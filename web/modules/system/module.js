export async function mount(ctx) {
    const available = value => value === null || value === undefined || value === '' ? 'Unavailable' : String(value);
    const yesNo = value => value ? 'Yes' : 'No';
    const setBadge = (selector, ok, good = 'Working', bad = 'Unavailable') => {
        const node = ctx.$(selector);
        if (!node) return;
        node.textContent = ok ? good : bad;
        node.className = `badge ${ok ? 'ok' : 'warn'}`;
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
            const network = ctx.$('#network-state');
            if (network) {
                network.textContent = diagnostics.ip_address ? 'Connected' : 'Unavailable';
                network.className = `badge ${diagnostics.ip_address ? 'ok' : 'warn'}`;
            }

            setBadge('#system-api-state', true);
            setBadge('#system-oled-state', Boolean(status.oled_ok));
            setBadge('#system-touch-state', Boolean(status.touch_ok));
            ctx.setText('#system-display-mode', available(status.display_mode));
            ctx.setText('#system-audio-state', status.alarm_active
                ? `Alarm playing at ${status.alarm_volume_percent || 0}%`
                : status.audio_playing
                    ? `Playing ${[status.audio_title, status.audio_artist].filter(Boolean).join(' - ') || status.audio_file || 'music'}`
                    : 'Stopped');

            const weather = status.weather || {};
            ctx.setText('#system-weather-state', Number(weather.observed_at) > 0
                ? `Updated ${weatherTime(weather.observed_at)}`
                : 'Waiting for data');
            ctx.setText('#system-weather-source', available(source.url || source.default_url));
            const latest = activity.status || {};
            ctx.setText('#system-weather-result', available(latest.result || (latest.ok ? 'success' : 'pending')));
            ctx.setText('#system-weather-message', available(latest.message));

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

    ctx.on('click', '#system-refresh', () => refresh());
    await refresh();
    const timer = window.setInterval(() => refresh().catch(() => {}), 10000);
    ctx.signal.addEventListener('abort', () => window.clearInterval(timer), {once: true});
    return {refresh};
}
