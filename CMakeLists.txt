if(${IDF_TARGET} STREQUAL esp8266)
    set(requires)
else()
    set(requires esp_wifi)
endif()
idf_component_register(SRCS "zh_espnow.c" INCLUDE_DIRS "include" REQUIRES ${requires})