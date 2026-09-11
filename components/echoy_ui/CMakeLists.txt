idf_component_register(
    SRCS "ui.c" "view.c" "touch.c" "i18n.c" "fonts.c"
    INCLUDE_DIRS "include"
    REQUIRES esp-mosaico-bsp lvgl__lvgl echoy_core
)
