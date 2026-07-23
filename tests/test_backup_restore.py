#!/usr/bin/env python3
from pathlib import Path
import json

root = Path(__file__).resolve().parents[1]
api = (root / 'mk-piclock-api.c').read_text(encoding='utf-8')
core = (root / 'mk-piclock.c').read_text(encoding='utf-8')
ipc = (root / 'ipc_protocol.h').read_text(encoding='utf-8')
html = (root / 'web/modules/system/module.html').read_text(encoding='utf-8')
js = (root / 'web/modules/system/module.js').read_text(encoding='utf-8')
spec = json.loads((root / 'api/openapi-v1.json').read_text(encoding='utf-8'))

for token in [
    '/api/v1/backup/download', '/api/v1/backup/restore',
    'build_adult_backup', 'extract_backup_archive', 'crc32_update',
    'stage_current_fonts', 'restore_old_fonts',
    'weather/source.url', 'weather/frames.conf', 'config/clock.conf'
]:
    assert token in api, token

backup_code = api[api.index('static int add_fonts_to_backup'):api.index('static enum MHD_Result queue_backup_file')]
assert 'MP_FONT_DIR' in backup_code
assert 'MP_MUSIC_DIR' not in backup_code
assert 'weather/source.url' in backup_code and 'weather/frames.conf' in backup_code

allowlist = api[api.index('static int restore_archive_path'):api.index('static int prepare_restore_stage')]
assert 'assets/fonts/' in allowlist
assert 'assets/music/' not in allowlist
assert 'weather/source.url' in allowlist and 'weather/frames.conf' in allowlist

assert 'MP_IPC_OP_CONFIG_EXPORT' in ipc and 'MP_IPC_OP_CONFIG_IMPORT' in ipc
assert 'ipc_config_export' in core and 'ipc_config_import' in core
assert 'reset_persistent_state_locked' in core
assert 'id="restore-form"' in html and 'Music, logs, caches' in html
assert "'#restore-form'" in js
assert '/api/v1/backup/download' in spec['paths']
assert '/api/v1/backup/restore' in spec['paths']
print('adult backup and restore checks passed')
