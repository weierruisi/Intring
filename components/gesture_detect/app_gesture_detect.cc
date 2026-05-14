#include "data_share.h"
#include "app_gesture_detect.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "imu_buffer.h"

#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/system_setup.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "model_config.h"
#include "gesture_model.h"
#include "test_data.h"
#include "data_share.h"

static const char* TAG = "app_gesture_detect";

CircularBuffer_t IMUbuffer;

TaskHandle_t app_gesture_detect_handle;
QueueHandle_t gesture_queue;
SemaphoreHandle_t app_gesture_detect_suspend_flag;
static int64_t gesture_discard_until_us = 0;

constexpr const char* kCategoryLabels[kCategoryCount] = {
    "left",
    "right",
    "up",
    "down",
    "O",
    "X",
    "D",
    "idle",
};

// Globals, used for compatibility with Arduino-style sketches.
namespace {
const tflite::Model* model = nullptr;
tflite::MicroInterpreter* interpreter = nullptr;
TfLiteTensor* model_input = nullptr;

// Create an area of memory to use for input, output, and intermediate arrays.
// The size of this will depend on the model you're using, and may need to be
// determined by experimentation.
constexpr int kTensorArenaSize = 30 * 1024;
uint8_t tensor_arena[kTensorArenaSize];
int8_t feature_buffer[kFeatureElementCount];
int8_t* model_input_buffer = nullptr;
}

extern "C" int8_t float2int(float value)
{
    float input_scale = model_input->params.scale;
    int input_zero_point = model_input->params.zero_point;

    return static_cast<int8_t>(value / input_scale + input_zero_point);
}

extern "C" void app_gesture_detect(void* ptr)
{
    while(1)
    {
        // Copy feature buffer to input tensor
        while(!IMUbuffer_isfull(&IMUbuffer))    vTaskDelay(10 / portTICK_PERIOD_MS);
        
        // if (xSemaphoreTake(IMUbuffer.bufMutex, portMAX_DELAY) == pdPASS){
            for (int i = 0; i < kFeatureElementCount; i++)
            {
                // float val = kRawImuData[count][i];
                // int8_t quantized_val = static_cast<int8_t>(val / input_scale + input_zero_point);
                // model_input_buffer[i] = quantized_val;
                
                IMUbuffer_get(&IMUbuffer, i, &model_input_buffer[i]);
            }
            // xSemaphoreGive(IMUbuffer.bufMutex);
        // }
        

        // Run the model on the spectrogram input and make sure it succeeds.
        TfLiteStatus invoke_status = interpreter->Invoke();
        if (invoke_status != kTfLiteOk) {
        MicroPrintf( "Invoke failed");
        return;
        }

        // Obtain a pointer to the output tensor
        TfLiteTensor* output = interpreter->output(0);

        float output_scale = output->params.scale;
        int output_zero_point = output->params.zero_point;
        int max_idx = 0;
        float max_result = 0.0;
        // Dequantize output values and find the max
        for (int i = 0; i < kCategoryCount; i++) {
        float current_result =
            (tflite::GetTensorData<int8_t>(output)[i] - output_zero_point) *
            output_scale;
        if (current_result > max_result) {
            max_result = current_result; // update max result
            max_idx = i; // update category
        }
        }
        if (max_result > 0.8f && strcmp(kCategoryLabels[max_idx], "idle")!=0) {
            printf("Detected %7s, score: %.2f\n",kCategoryLabels[max_idx],
            static_cast<double>(max_result));
            if (esp_timer_get_time() < gesture_discard_until_us) {
                ESP_LOGI(TAG, "discard gesture during warmup window");
                IMUbuffer_clear(&IMUbuffer);
                vTaskDelay(10 / portTICK_PERIOD_MS);
                continue;
            }
            // if(xSemaphoreTake(app_gesture_detect_suspend_flag, 0))
            // {
            //     //抛弃这次的检测结果
            //     ESP_LOGW(TAG, "give up current result");
            //     max_idx = 0;
            // }
            gesture_class_t ges_res = (gesture_class_t)max_idx;
            xQueueSend(gesture_queue, &ges_res, 0);
            
            // if (xSemaphoreTake(IMUbuffer.bufMutex, portMAX_DELAY) == pdPASS){
                IMUbuffer_clear(&IMUbuffer);
                // xSemaphoreGive(IMUbuffer.bufMutex);
            // }
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

extern "C" void app_gesture_detect_discard_for_ms(uint32_t duration_ms)
{
    int64_t now_us = esp_timer_get_time();
    gesture_discard_until_us = now_us + ((int64_t)duration_ms * 1000);
}

extern "C" void app_gesture_detect_init(void)
{
    // 二值信号量初始化
    app_gesture_detect_suspend_flag = xSemaphoreCreateBinary();
    if(app_gesture_detect_suspend_flag == NULL)
    {ESP_LOGE(TAG, "app_gesture_detect_suspend_flag err");}
    // 消息队列初始化
    gesture_queue = xQueueCreate(1, sizeof(gesture_class_t));

    // 循环缓冲区初始化
    IMUbuffer_init(&IMUbuffer);

    model = tflite::GetModel(gesture_model_quant_tflite);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
    MicroPrintf("Model provided is schema version %d not equal to supported "
                "version %d.", model->version(), TFLITE_SCHEMA_VERSION);
    return;
    }

    // 将容量从 4 改为 10，防止后续添加算子时溢出
    static tflite::MicroMutableOpResolver<11> micro_op_resolver;

    // 1. Conv1D 在 TFLite 中通常映射为 Conv2D
    if (micro_op_resolver.AddConv2D() != kTfLiteOk) {
        ESP_LOGE(TAG, "AddConv2D failed");
        return;
    }

    // 2. 对应图中的 MaxPooling1D
    if (micro_op_resolver.AddMaxPool2D() != kTfLiteOk) {
        ESP_LOGE(TAG, "AddMaxPool2D failed");
        return;
    }

    // 3. 对应图中的 GlobalAveragePooling1D
    if (micro_op_resolver.AddAveragePool2D() != kTfLiteOk) {
        ESP_LOGE(TAG, "AddAveragePool2D failed");
        return;
    }

    // // 4. 对应图中的 BatchNormalization (在量化模型中通常会被融合，但建议加上以防万一)
    // if (micro_op_resolver.AddBatchNormalization() != kTfLiteOk) {
    //     // 注：如果是完全量化的模型，该层可能已融合进 Conv2D，若报错可尝试注释掉
    //     ESP_LOGE(TAG, "AddBatchNormalization failed");
    // }

    // 5. 对应图中的 Dense 层
    if (micro_op_resolver.AddFullyConnected() != kTfLiteOk) {
        ESP_LOGE(TAG, "AddFullyConnected failed");
        return;
    }

    // 6. 对应图中未显示但通常存在的激活层 (如 ReLU)
    if (micro_op_resolver.AddRelu() != kTfLiteOk) {
        ESP_LOGE(TAG, "AddRelu failed");
    }

    // 7. 对应输出层的概率转换
    if (micro_op_resolver.AddSoftmax() != kTfLiteOk) {
        ESP_LOGE(TAG, "AddSoftmax failed");
        return;
    }

    // 8. Reshape (虽然图中没写，但通常 GlobalPooling 后会有一个隐形的 Reshape)
    if (micro_op_resolver.AddReshape() != kTfLiteOk) {
        ESP_LOGE(TAG, "AddReshape failed");
        return;
    }

    if (micro_op_resolver.AddExpandDims() != kTfLiteOk) {
        ESP_LOGE(TAG, "AddExpandDims failed");
        return;
    }

    if (micro_op_resolver.AddMul() != kTfLiteOk) {
        ESP_LOGE(TAG, "AddMul failed");
        return;
    }

    if (micro_op_resolver.AddAdd() != kTfLiteOk) {
        ESP_LOGE(TAG, "AddAdd failed");
        return;
    }

    if (micro_op_resolver.AddMean() != kTfLiteOk) {
        ESP_LOGE(TAG, "AddMean failed");
        return;
    }

    // Build an interpreter to run the model with.
    static tflite::MicroInterpreter static_interpreter(
        model, micro_op_resolver, tensor_arena, kTensorArenaSize);
    interpreter = &static_interpreter;

    // Allocate memory from the tensor_arena for the model's tensors.
    TfLiteStatus allocate_status = interpreter->AllocateTensors();
    if (allocate_status != kTfLiteOk) {
        MicroPrintf("AllocateTensors() failed");
        return;
    }

    // Get information about the memory area to use for the model's input.
    model_input = interpreter->input(0);
    // 适配 (1, 128, 3) 这种三维输入
    if ((model_input->dims->size != 3) ||
        (model_input->dims->data[0] != 1) ||
        (model_input->dims->data[1] != kFeatureCount) ||
        (model_input->dims->data[2] != kFeatureSize) ||
        (model_input->type != kTfLiteInt8)) {
        MicroPrintf("Bad input tensor parameters in model");
        return;
    }
    model_input_buffer = tflite::GetTensorData<int8_t>(model_input);

    // // Prepare to access the audio spectrograms from a microphone or other source
    // // that will provide the inputs to the neural network.
    // // NOLINTNEXTLINE(runtime-global-variables)
    // static FeatureProvider static_feature_provider(kFeatureElementCount,
    //                                                feature_buffer);
    // feature_provider = &static_feature_provider;

    // static RecognizeCommands static_recognizer;
    // recognizer = &static_recognizer;

    // previous_time = 0;

    // 在 while 循环外先获取输入量化参数

#if CONFIG_TEST_AI_MODEL_EN

    while (1) {
        for(int count = 0; count < 4; count++)
        {
            // Copy feature buffer to input tensor
            for (int i = 0; i < kFeatureElementCount; i++)
            {
                // float val = kRawImuData[count][i];
                // int8_t quantized_val = static_cast<int8_t>(val / input_scale + input_zero_point);
                // model_input_buffer[i] = quantized_val;
                
                model_input_buffer[i] = float2int(kRawImuData[count][i]);
            }

            // Run the model on the spectrogram input and make sure it succeeds.
            TfLiteStatus invoke_status = interpreter->Invoke();
            if (invoke_status != kTfLiteOk) {
            MicroPrintf( "Invoke failed");
            return;
            }

            // Obtain a pointer to the output tensor
            TfLiteTensor* output = interpreter->output(0);

            float output_scale = output->params.scale;
            int output_zero_point = output->params.zero_point;
            int max_idx = 0;
            float max_result = 0.0;
            // Dequantize output values and find the max
            for (int i = 0; i < kCategoryCount; i++) {
            float current_result =
                (tflite::GetTensorData<int8_t>(output)[i] - output_zero_point) *
                output_scale;
            if (current_result > max_result) {
                max_result = current_result; // update max result
                max_idx = i; // update category
            }
            }
            if (max_result > 0.8f) {
            printf("%dDetected %7s, score: %.2f\n",count, kCategoryLabels[max_idx],
                static_cast<double>(max_result));
            }
        }

        
        while(1)
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
#endif

    xTaskCreate(&app_gesture_detect, "app_gesture_detect", 4096, NULL, 4, &app_gesture_detect_handle);
    vTaskSuspend(app_gesture_detect_handle);
}
