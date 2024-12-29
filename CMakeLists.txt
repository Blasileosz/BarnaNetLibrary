FILE(GLOB_RECURSE component_sources ${COMPONENT_DIR}/*.*)

idf_component_register(
	SRCS ${component_sources}
	INCLUDE_DIRS "."
	REQUIRES driver lwip esp_wifi nvs_flash
)
