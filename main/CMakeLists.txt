idf_component_register(
    SRCS
        "app_events.c"
        "audio_state.c"
        "ble_transport.c"
        "frame_reassembly.c"
        "main.c"
        "protocol_decoder.c"
        "time_sync.c"
    INCLUDE_DIRS "."
    REQUIRES bt nvs_flash
)
