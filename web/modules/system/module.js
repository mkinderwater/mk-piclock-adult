export async function mount(ctx) {
    const available = value => value === null || value === undefined || value === '' ? 'Unavailable' : String(value);
    const yesNo = value => value ? 'Yes' : 'No';
    const setBadge = (selector, ok, good = 'Working', bad = 'Unavailable') => {
        const node = ctx.$(selector);
        if (!node) return;
        node.textContent = ok ? good : bad;
        node.className = `badge ${ok ? 'ok' : 'warn'}`;
    };
    const bytes = value => {
        const amount = Number(value);
        if (!Number.isFinite(amount) || amount < 0) return 'Unavailable';
        const units = ['B', 'KB', 'MB', 'GB', 'TB'];
        let size = amount; let index = 0;
        while (size >= 1024 && index < units.length - 1) { size /= 1024; index++; }
        return `${size.toFixed(index < 2 ? 0 : 1)} ${units[index]}`;
    };
    const directoryUsage = (size, count) => {
        const files = Math.max(0, Number(count || 0));
        return `${bytes(size)} in ${files.toLocaleString()} file${files === 1 ? '' : 's'}`;
    };
    const weatherTime = value => {
        const epoch = Number(value || 0);
        return epoch > 0 ? new Date(epoch * 1000).toLocaleString() : 'Waiting for data';
    };

    const refresh = async () => {
        const button = ctx.$('#system-refresh');
        if (button) ctx.busy(button, true, 'Refreshing...');
        try {
            const [discovery, status, capabilities, source, activity, diagnostics] = await Promise.all([
                ctx.json('/api/v1', {signal: ctx.signal}),
                ctx.json('/api/v1/status', {signal: ctx.signal}),
                ctx.json('/api/v1/capabilities', {signal: ctx.signal}),
                ctx.json('/api/v1/config/weather-source', {signal: ctx.signal}),
                ctx.json('/api/v1/weather/activity', {signal: ctx.signal}),
                ctx.json('/api/v1/diagnostics', {signal: ctx.signal})
            ]);

            ctx.setText('#system-product', available(discovery.name || 'mk-clock-adult'));
            ctx.setText('#system-version', available(discovery.product_version || status.app_version));
            ctx.setText('#system-api-version', available(discovery.api_version || capabilities.api_version));
            ctx.setText('#system-core-protocol', available(discovery.core_protocol));
            ctx.setText('#system-uptime', ctx.formatUptime(status.uptime_seconds));
            ctx.setText('#system-time', [status.time, status.date].filter(Boolean).join(' · ') || 'Unavailable');

            ctx.setText('#diag-ip', available(diagnostics.ip_address));
            ctx.setText('#diag-hostname', available(diagnostics.hostname));
            ctx.setText('#diag-ssid', available(diagnostics.ssid));
            ctx.setText('#diag-interface', available(diagnostics.interface));
            ctx.setText('#diag-signal', diagnostics.wifi_signal_available
                ? `${diagnostics.wifi_signal_percent}% (${diagnostics.wifi_signal_dbm} dBm)`
                : 'Unavailable');
            ctx.setText('#diag-ntp', yesNo(diagnostics.ntp_synchronized));
            ctx.setText('#diag-time-valid', yesNo(diagnostics.system_time_valid));
            ctx.setText('#diag-product-version', available(diagnostics.product_version));
            ctx.setText('#diag-api-version', available(diagnostics.api_version));
            ctx.setText('#diag-weather-version', available(diagnostics.weather_version));
            ctx.setText('#diag-compiled', available(diagnostics.compiled_at));
            ctx.setText('#diag-hardware', available(diagnostics.hardware_model));
            ctx.setText('#diag-os', available(diagnostics.os_pretty_name));
            ctx.setText('#diag-os-release', [diagnostics.os_version_id, diagnostics.os_codename].filter(Boolean).join(' / ') || 'Unavailable');
            ctx.setText('#diag-kernel', available(diagnostics.kernel_release));
            ctx.setText('#diag-architecture', available(diagnostics.architecture));
            ctx.setText('#diag-temperature', Number(diagnostics.cpu_temperature_c) ? `${Number(diagnostics.cpu_temperature_c).toFixed(1)} °C` : 'Unavailable');
            ctx.setText('#diag-inventory-id', available(diagnostics.inventory_id));
            ctx.setText('#diag-pi-serial', available(diagnostics.pi_serial));
            ctx.setText('#diag-board-revision', available(diagnostics.board_revision));
            ctx.setText('#diag-machine-id', available(diagnostics.machine_id));
            ctx.setText('#diag-cpu-signature', available(diagnostics.cpu_signature));
            ctx.setText('#diag-root-disk', available(diagnostics.root_disk));
            ctx.setText('#diag-root-device', available(diagnostics.root_device));
            ctx.setText('#diag-root-filesystem', available(diagnostics.root_filesystem));
            ctx.setText('#diag-root-state', diagnostics.root_device ? (diagnostics.root_read_only ? 'Read-only' : 'Read/write') : 'Unavailable');
            const storagePercent = Number(diagnostics.storage_total_bytes) > 0 ? ` (${((Number(diagnostics.storage_used_bytes || 0) / Number(diagnostics.storage_total_bytes)) * 100).toFixed(1)}%)` : '';
            ctx.setText('#diag-storage-used', `${bytes(diagnostics.storage_used_bytes)}${storagePercent}`);
            ctx.setText('#diag-storage-available', bytes(diagnostics.storage_free_bytes));
            ctx.setText('#diag-storage-total', bytes(diagnostics.storage_total_bytes));
            ctx.setText('#diag-music-size', directoryUsage(diagnostics.music_bytes, diagnostics.music_files));
            ctx.setText('#diag-fonts-size', directoryUsage(diagnostics.fonts_bytes, diagnostics.fonts_files));
            ctx.setText('#diag-config-size', directoryUsage(diagnostics.config_bytes, diagnostics.config_files));
            ctx.setText('#diag-boot-device', available(diagnostics.boot_device));
            ctx.setText('#diag-boot-filesystem', available(diagnostics.boot_filesystem));
            ctx.setText('#diag-boot-mount', available(diagnostics.boot_mount_point));
            ctx.setText('#diag-sd-device', available(diagnostics.sd_device));
            ctx.setText('#diag-sd-type', available(diagnostics.sd_type));
            ctx.setText('#diag-sd-name', available(diagnostics.sd_name));
            ctx.setText('#diag-sd-capacity', bytes(diagnostics.sd_capacity_bytes));
            ctx.setText('#diag-sd-manufacturer', available(diagnostics.sd_manufacturer_id));
            ctx.setText('#diag-sd-oem', available(diagnostics.sd_oem_id));
            ctx.setText('#diag-sd-serial', available(diagnostics.sd_serial));
            ctx.setText('#diag-sd-date', available(diagnostics.sd_manufacture_date));
            ctx.setText('#diag-sd-cid', available(diagnostics.sd_cid));
            const sdState = ctx.$('#sd-card-state');
            if (sdState) { sdState.textContent = diagnostics.sd_present ? 'Detected' : 'Unavailable'; sdState.className = `badge ${diagnostics.sd_present ? 'ok' : 'warn'}`; }
            ctx.setText('#diag-room-status', available(diagnostics.room_sensor_status));
            ctx.setText('#diag-room-temperature', diagnostics.room_measured_at ? `${Number(diagnostics.room_temperature_c).toFixed(1)} °C` : 'Unavailable');
            ctx.setText('#diag-room-humidity', diagnostics.room_measured_at ? `${Number(diagnostics.room_humidity_percent).toFixed(1)}%` : 'Unavailable');
            ctx.setText('#diag-room-measured', weatherTime(diagnostics.room_measured_at));
            ctx.setText('#diag-room-error', diagnostics.room_sensor_error || 'None');
            const ntpWarning = ctx.$('#system-ntp-warning');
            if (ntpWarning) ntpWarning.classList.toggle('hidden', Boolean(diagnostics.ntp_synchronized && diagnostics.system_time_valid));
            const network = ctx.$('#network-state');
            if (network) {
                network.textContent = diagnostics.ip_address ? 'Connected' : 'Unavailable';
                network.className = `badge ${diagnostics.ip_address ? 'ok' : 'warn'}`;
            }

            setBadge('#system-api-state', Boolean(diagnostics.api_healthy));
            ctx.setText('#system-core-state', diagnostics.core_healthy ? 'Working' : 'Unavailable');
            setBadge('#system-oled-state', Boolean(status.oled_ok));
            setBadge('#system-touch-state', Boolean(status.touch_ok));
            ctx.setText('#system-display-mode', available(status.display_mode));
            ctx.setText('#system-next-alarm', status.next_alarm_text || 'No alarm scheduled');
            ctx.setText('#system-last-alarm', Number(status.last_successful_alarm || 0) > 0
                ? new Date(Number(status.last_successful_alarm) * 1000).toLocaleString()
                : 'Never');
            ctx.setText('#system-audio-state', status.alarm_active
                ? `Alarm playing at ${status.alarm_volume_percent || 0}%`
                : status.audio_playing
                    ? `Playing ${[status.audio_title, status.audio_artist].filter(Boolean).join(' - ') || status.audio_file || 'music'}`
                    : 'Stopped');

            const weather = status.weather || {};
            ctx.setText('#system-weather-state', Number(weather.observed_at) > 0
                ? `Updated ${weatherTime(weather.observed_at)}`
                : 'Waiting for data');
            ctx.setText('#system-weather-source', available(diagnostics.weather_source_url || source.url || source.default_url));
            const latest = activity.status || {};
            ctx.setText('#system-weather-result', available(diagnostics.weather_result || latest.result || (latest.ok ? 'success' : 'pending')));
            ctx.setText('#system-weather-message', available(diagnostics.weather_message || latest.message));

            const list = Array.isArray(capabilities.capabilities) ? capabilities.capabilities : [];
            const holder = ctx.$('#system-capabilities');
            if (holder) holder.innerHTML = list.length
                ? list.map(item => `<span class="system-capability">${ctx.html(item)}</span>`).join('')
                : '<span class="muted">No capabilities reported.</span>';
        } catch (error) {
            setBadge('#system-api-state', false);
            ctx.notice(error.message || 'System information could not be loaded', 'warn', 3500);
        } finally {
            if (button) ctx.busy(button, false);
        }
    };


    ctx.on('submit', '#restore-form', async (event, form) => {
        event.preventDefault();
        if (!window.confirm('Restore this backup? Settings, alarms, fonts, and Weather configuration will be replaced. Music will remain unchanged.')) return;
        const button = form.querySelector('[type="submit"]');
        try {
            await ctx.update(form.action, {
                method: 'POST',
                body: new FormData(form),
                button,
                busyText: 'Restoring backup...',
                done: 'Backup restored',
                errorText: 'Backup could not be restored'
            });
            form.reset();
            await refresh();
        } catch (_) {}
    });

    ctx.on('click', '#system-refresh', () => refresh());
    await refresh();
    const timer = window.setInterval(() => refresh().catch(() => {}), 10000);
    ctx.signal.addEventListener('abort', () => window.clearInterval(timer), {once: true});
    return {refresh};
}
