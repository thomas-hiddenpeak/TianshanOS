/**
 * @file ts_led_font.h
 * @brief TianShanOS LED Font Management
 * 
 * Dynamic font loading system for LED matrix text display.
 * Supports loading bitmap fonts from SD card with LRU glyph caching.
 * 
 * Font file format (.fnt):
 *   - Header: 16 bytes (magic, dimensions, glyph count)
 *   - Index: sorted by codepoint for binary search
 *   - Bitmap: packed bits, ceil(w*h/8) bytes per glyph
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 */

#ifndef TS_LED_FONT_H
#define TS_LED_FONT_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================*/
/*                              Constants                                     */
/*===========================================================================*/

/** Font file magic identifier */
#define TS_FONT_MAGIC           "TFNT"

/** Maximum font dimensions */
#define TS_FONT_MAX_SIZE        16

/** Default glyph cache size (number of glyphs) */
#define TS_FONT_CACHE_SIZE      64

/** Maximum path length for font files */
#define TS_FONT_PATH_MAX        64

/*===========================================================================*/
/*                              Types                                         */
/*===========================================================================*/

/**
 * @brief Font file header structure (16 bytes)
 */
typedef struct __attribute__((packed)) {
    char magic[4];          /**< "TFNT" */
    uint8_t version;        /**< Format version (1) */
    uint8_t width;          /**< Glyph width in pixels */
    uint8_t height;         /**< Glyph height in pixels */
    uint8_t flags;          /**< Flags (reserved) */
    uint32_t glyph_count;   /**< Number of glyphs in font */
    uint32_t index_offset;  /**< Offset to index table */
} ts_font_header_t;

/**
 * @brief Font index entry structure (6 bytes)
 */
typedef struct __attribute__((packed)) {
    uint16_t codepoint;     /**< Unicode codepoint */
    uint32_t offset;        /**< Offset to bitmap data */
} ts_font_index_entry_t;

/**
 * @brief Cached glyph data
 */
typedef struct {
    uint16_t codepoint;     /**< Unicode codepoint */
    uint8_t bitmap[32];     /**< Bitmap data (max 16x16/8 = 32 bytes) */
    uint32_t last_used;     /**< Last access time for LRU */
} ts_font_glyph_cache_t;

/**
 * @brief Font handle structure
 */
typedef struct ts_font {
    FILE *fp;                       /**< File handle */
    ts_font_header_t header;        /**< Font header */
    char path[TS_FONT_PATH_MAX];    /**< File path */
    
    /* Index cache (optional, for fast ASCII lookup) */
    ts_font_index_entry_t *ascii_index; /**< Cached ASCII index (95 entries) */
    
    /* Glyph cache (LRU) */
    ts_font_glyph_cache_t *cache;   /**< Glyph cache array */
    uint8_t cache_size;             /**< Cache capacity */
    uint8_t cache_count;            /**< Current cache usage */
    
    /* Statistics */
    uint32_t cache_hits;            /**< Cache hit count */
    uint32_t cache_misses;          /**< Cache miss count */
} ts_font_t;

/**
 * @brief Font configuration
 */
typedef struct {
    uint8_t cache_size;             /**< LRU cache size (0=disabled, default=64) */
    bool cache_ascii;               /**< Pre-cache ASCII glyphs (default=true) */
} ts_font_config_t;

/** Default font configuration */
#define TS_FONT_DEFAULT_CONFIG() { \
    .cache_size = TS_FONT_CACHE_SIZE, \
    .cache_ascii = true \
}

/*===========================================================================*/
/*                              API Functions                                 */
/*===========================================================================*/

/**
 * @brief Load font from file
 * 
 * @param path Path to .fnt file (e.g., "/sdcard/fonts/boutique9x9.fnt")
 * @param config Font configuration (NULL for defaults)
 * @return Font handle, or NULL on failure
 */
ts_font_t *ts_font_load(const char *path, const ts_font_config_t *config);

/**
 * @brief Unload font and free resources
 * 
 * @param font Font handle
 */
void ts_font_unload(ts_font_t *font);

/**
 * @brief Get glyph bitmap for a character
 * 
 * Returns pointer to bitmap data. Uses cache if available.
 * Bitmap format: row-major, MSB first, packed bits.
 * 
 * @param font Font handle
 * @param codepoint Unicode codepoint
 * @param bitmap Output: pointer to bitmap data (valid until next call)
 * @return ESP_OK if glyph found, ESP_ERR_NOT_FOUND if missing
 */
esp_err_t ts_font_get_glyph(ts_font_t *font, uint16_t codepoint, 
                             const uint8_t **bitmap);

/**
 * @brief Check if font has a specific character
 * 
 * @param font Font handle
 * @param codepoint Unicode codepoint
 * @return true if glyph exists
 */
bool ts_font_has_glyph(ts_font_t *font, uint16_t codepoint);

/**
 * @brief Get font dimensions
 * 
 * @param font Font handle
 * @param width Output: glyph width
 * @param height Output: glyph height
 * @return ESP_OK on success
 */
esp_err_t ts_font_get_size(ts_font_t *font, uint8_t *width, uint8_t *height);

/**
 * @brief Get font glyph count
 * 
 * @param font Font handle
 * @return Number of glyphs in font
 */
uint32_t ts_font_get_glyph_count(ts_font_t *font);

/**
 * @brief Clear font glyph cache
 * 
 * @param font Font handle
 */
void ts_font_clear_cache(ts_font_t *font);

/**
 * @brief Get cache statistics
 * 
 * @param font Font handle
 * @param hits Output: cache hits
 * @param misses Output: cache misses
 */
void ts_font_get_stats(ts_font_t *font, uint32_t *hits, uint32_t *misses);

/**
 * @brief Get currently loaded fonts memory usage
 * 
 * @return Memory usage in bytes
 */
size_t ts_font_get_memory_usage(void);

#ifdef __cplusplus
}
#endif

#endif /* TS_LED_FONT_H */
