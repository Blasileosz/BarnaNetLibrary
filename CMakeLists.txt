# https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/build-system.html#component-cmakelists-files
FILE(GLOB_RECURSE component_sources ${COMPONENT_DIR}/*.*)

idf_component_register(
	SRCS ${component_sources}
	INCLUDE_DIRS "."
	REQUIRES driver lwip esp_wifi nvs_flash mqtt esp_http_server json
	EMBED_TXTFILES certs/DigiCertGlobalRootG2.crt.pem certs/BB0.crt certs/BB0.key # https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/build-system.html#embedding-binary-data
)
