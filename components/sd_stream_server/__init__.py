"""Read-only, streaming HTTP file server for an already-mounted SD card.

Exists because n-serrette/esphome_sd_card's sd_file_server crashes the board
on download (StoreProhibited fault, reproduced on an Olimex
ESP32-POE-ISO under ESP-IDF). Rather than carry patches against a component
that also does listing, upload, deletion and MIME detection, this serves
exactly one operation: GET a path, stream it back.

It deliberately does NOT depend on sd_mmc_card. Once that component has
mounted the card, the card is just a VFS path, so this only needs
web_server_base — which also means the two can be upgraded independently.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import web_server_base
from esphome.components.web_server_base import CONF_WEB_SERVER_BASE_ID
from esphome.const import CONF_ID
from esphome.core import coroutine_with_priority

CONF_URL_PREFIX = "url_prefix"
CONF_ROOT_PATH = "root_path"
CONF_CHUNK_SIZE = "chunk_size"

AUTO_LOAD = ["web_server_base"]

sd_stream_server_ns = cg.esphome_ns.namespace("sd_stream_server")
SdStreamServer = sd_stream_server_ns.class_("SdStreamServer", cg.Component)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(SdStreamServer),
            cv.GenerateID(CONF_WEB_SERVER_BASE_ID): cv.use_id(
                web_server_base.WebServerBase
            ),
            cv.Optional(CONF_URL_PREFIX, default="file"): cv.string_strict,
            # Where sd_mmc_card's ESP-IDF implementation mounts the card. It
            # does not expose this, so it is configurable here rather than
            # assumed — if that ever changes, this is the one line to edit.
            cv.Optional(CONF_ROOT_PATH, default="/sdcard"): cv.string_strict,
            # Bytes read from the card and flushed per chunk. 4 KiB is a
            # typical FAT cluster size and keeps peak RAM flat regardless of
            # file size, which is the whole point of streaming.
            cv.Optional(CONF_CHUNK_SIZE, default=4096): cv.int_range(
                min=512, max=32768
            ),
        }
    ).extend(cv.COMPONENT_SCHEMA),
)


@coroutine_with_priority(45.0)
async def to_code(config):
    paren = await cg.get_variable(config[CONF_WEB_SERVER_BASE_ID])
    var = cg.new_Pvariable(config[CONF_ID], paren)
    await cg.register_component(var, config)
    cg.add(var.set_url_prefix(config[CONF_URL_PREFIX]))
    cg.add(var.set_root_path(config[CONF_ROOT_PATH]))
    cg.add(var.set_chunk_size(config[CONF_CHUNK_SIZE]))
