idf_component_register(SRCS "keyRead.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver
                                freertos
                                main)
