set(srcs "i2c.c")

idf_component_register(SRCS "${srcs}"
                        INCLUDE_DIRS "."
                        REQUIRES driver)