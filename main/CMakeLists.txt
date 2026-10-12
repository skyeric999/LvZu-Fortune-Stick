idf_component_register(
    SRCS "main.c"
         "qian_data.c"
         "shengbei_model.c"
         "shengbei_store.c"
         "sfx_tones.c"
         "sfx_player.c"
         "ui_shengbei.c"
         "assets/fonts/app_font_16.c"
         "assets/fonts/app_font_26.c"
         "assets/images/cup_photo.c"
    INCLUDE_DIRS "."
    REQUIRES bsp lvgl nvs_flash esp_timer
)
