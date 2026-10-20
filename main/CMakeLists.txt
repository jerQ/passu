# ESP-IDF reads this file twice: once in an early dependency scan (where
# CMAKE_BUILD_EARLY_EXPANSION is set and most variables are not), then for
# the real build. Only check for the wordlist in the real pass.
# CMAKE_CURRENT_LIST_DIR always points at this file's directory.
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/eff_large_wordlist.txt")
        message(FATAL_ERROR
            "Missing main/eff_large_wordlist.txt - download it from "
            "https://www.eff.org/files/2016/07/18/eff_large_wordlist.txt")
    endif()
    # Catch a tampered or corrupted wordlist at build time; the firmware
    # checks the same hash again at boot.
    if(NOT "${CONFIG_DW_WORDLIST_SHA256}" STREQUAL "")
        file(SHA256 "${CMAKE_CURRENT_LIST_DIR}/eff_large_wordlist.txt" wl_hash)
        string(TOLOWER "${CONFIG_DW_WORDLIST_SHA256}" wl_expected)
        if(NOT wl_hash STREQUAL wl_expected)
            message(FATAL_ERROR
                "main/eff_large_wordlist.txt has SHA-256 ${wl_hash}, "
                "expected ${wl_expected} (CONFIG_DW_WORDLIST_SHA256)")
        endif()
    endif()

    # Secure Boot burns eFuses on first boot, which can never be undone.
    # Refuse to build until that has been acknowledged, and say exactly what
    # will be burned either way.
    if(CONFIG_SECURE_BOOT)
        set(lockdown_changes
            "PERMANENT eFuse changes on the first boot of this firmware:\n"
            "  - BLOCK2 key digest: only firmware signed with "
            "${CONFIG_SECURE_BOOT_SIGNING_KEY} will ever run. Lose the key "
            "and the board can never be updated.\n"
            "  - ABS_DONE_1: Secure Boot is on for good.\n"
            "  - JTAG_DISABLE: JTAG debugging is gone for good.\n"
            "  - CONSOLE_DEBUG_DISABLE: ROM BASIC console is gone for good.\n")
        if(NOT CONFIG_SECURE_BOOT_V2_ALLOW_EFUSE_RD_DIS)
            list(APPEND lockdown_changes
                "  - WR_DIS_EFUSE_RD_DISABLE: no eFuse can be read-protected "
                "later, so flash encryption can't be added.\n")
        endif()
        if(CONFIG_SECURE_DISABLE_ROM_DL_MODE)
            list(APPEND lockdown_changes
                "  - UART_DOWNLOAD_DIS: esptool and espefuse can never connect "
                "again. No reflashing and no recovery over USB.\n")
        endif()
        list(APPEND lockdown_changes
            "None of this can be undone. See \"Locking down a device\" in SECURITY.md.\n")
        string(CONCAT lockdown_msg ${lockdown_changes})
        if(NOT CONFIG_DW_LOCKDOWN_ACK)
            message(FATAL_ERROR
                "Secure Boot is enabled but not acknowledged.\n${lockdown_msg}"
                "To go ahead, enable \"I understand that Secure Boot eFuse "
                "changes are PERMANENT\" (CONFIG_DW_LOCKDOWN_ACK) under "
                "passu in menuconfig.")
        endif()
        message(WARNING "${lockdown_msg}")
    endif()
endif()

idf_component_register(
    SRCS "main.c" "entropy_radio.c" "health.c" "condition.c" "dice.c" "wordlist.c" "passphrase.c"
         "display.c" "oled.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_driver_i2c esp_wifi esp_event esp_netif mbedtls
    EMBED_TXTFILES "eff_large_wordlist.txt"
)
