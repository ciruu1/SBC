idf_component_register(SRCS "main.c" "spi_mod.c" "claves.c" "telegram.c"
                    INCLUDE_DIRS "."
        EMBED_TXTFILES telegram_certificate.pem)
