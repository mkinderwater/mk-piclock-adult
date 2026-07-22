export async function mount(ctx) {
    const preview = ctx.createOledPreview(ctx.$('#oled-screen-preview'));
    let previewLoading = false;
    let previewDelayMs = 250;
    let previewTimer = 0;

    const setPreviewState = (text, type = '') => {
        const node = ctx.$('#oled-preview-state');
        if (!node) return;
        node.textContent = text;
        node.className = `badge ${type}`.trim();
    };

    const refreshPreview = async () => {
        if (previewLoading || ctx.signal.aborted) return;
        previewLoading = true;
        try {
            preview.draw(await ctx.binary('/api/v1/display/preview', {signal: ctx.signal}));
            setPreviewState('Live', 'ok');
        } catch (error) {
            if (!ctx.signal.aborted) setPreviewState('Unavailable', 'warn');
        } finally {
            previewLoading = false;
        }
    };

    const weatherIconName = value => ({
        1: 'Clear',
        2: 'Partly cloudy',
        3: 'Cloudy',
        4: 'Rain',
        5: 'Storm',
        6: 'Snow',
        7: 'Wind',
        8: 'Fog'
    })[Number(value)] || 'Unknown';

    const render = status => {
        if (!status) return;
        preview.setColour(status.oled_color);
        ctx.setText('#status-time', status.time);
        ctx.setText('#status-date', status.date);
        ctx.setText('#status-name', status.clock_name);
        ctx.setText('#status-version', status.app_version);
        ctx.setText('#status-uptime', ctx.formatUptime(status.uptime_seconds));
        const track = [status.audio_title, status.audio_artist].filter(Boolean).join(' - ') || status.audio_file;
        ctx.setText('#status-audio', status.alarm_active
            ? `Alarm playing at ${status.alarm_volume_percent || 0}%`
            : (status.audio_playing ? `Playing ${track || 'music'}` : 'Audio stopped'));
        ctx.setText('#status-volume', status.alarm_active
            ? `Alarm ${status.alarm_volume_percent || 0}%`
            : `${status.global_volume || 0}%`);
        ctx.setText('#status-display', status.display_mode);
        ctx.setText('#status-oled-color', `${String(status.oled_color || 'green').replace(/^./, value => value.toUpperCase())} panel`);
        ctx.setText('#status-touch', status.touch_ok
            ? (status.touch_pressed ? `Pressed on GPIO ${status.touch_gpio}` : `Ready on GPIO ${status.touch_gpio}`)
            : `Unavailable on GPIO ${status.touch_gpio ?? 20}`);
        ctx.setText('#status-font', status.oled_font_name);
        ctx.setText('#status-bedtime', status.bedtime_enabled
            ? `${ctx.timeValue(status.bedtime_start_hour, status.bedtime_start_min)} to ${ctx.timeValue(status.bedtime_end_hour, status.bedtime_end_min)}, ${status.bedtime_dim_percent}%, ${status.clock_24h_mode ? '24-hour' : '12-hour'}`
            : 'Off');
        ctx.setText('#summary-clock', status.oled_ok ? `Working · ${status.time || ''}` : 'Screen unavailable');
        ctx.setText('#summary-sound', status.alarm_active
            ? `Alarm playing · ${status.alarm_volume_percent || 0}%`
            : (status.audio_playing ? `Playing ${track || 'music'}` : 'Quiet'));
        ctx.setText('#summary-bedtime', status.bedtime_enabled
            ? `${ctx.timeValue(status.bedtime_start_hour, status.bedtime_start_min)} to ${ctx.timeValue(status.bedtime_end_hour, status.bedtime_end_min)}`
            : 'Not scheduled');

        const room = status.room_sensor || {};
        const roomHasReading = ['active', 'stale'].includes(String(room.status || ''));
        const roomReading = roomHasReading
            ? `${Number(room.temperature_c).toFixed(1)}°C · ${Math.round(Number(room.humidity_percent) || 0)}% RH`
            : (room.status === 'disabled' ? 'Disabled' : room.status === 'waiting' ? 'Starting' : 'Unavailable');
        ctx.setText('#summary-room', roomReading);
        ctx.setText('#status-room-sensor', roomHasReading
            ? `${roomReading}${room.status === 'stale' ? ' · stale' : ''} · AHT10 on ${room.device || '/dev/i2c-1'}`
            : `${roomReading}${room.error ? ` · ${room.error}` : ''}`);

        const weather = status.weather || {};
        const slots = Array.isArray(weather.slots) ? weather.slots : [];
        const currentTemperature = Number(weather.current_temperature_c);
        const currentAvailable = weather.current_temperature_available === undefined
            ? Number.isFinite(currentTemperature) && Number(weather.observed_at) > 0
            : Boolean(weather.current_temperature_available);
        const currentPrecipitation = Math.max(0, Math.min(100,
            Math.round(Number(weather.precipitation_probability_percent) || 0)));
        const humidityAvailable = weather.humidity_available === undefined
            ? Number.isFinite(Number(weather.humidity_percent))
            : Boolean(weather.humidity_available);
        const weatherSummary = currentAvailable
            ? `${Math.round(currentTemperature)}°C · ${currentPrecipitation}% precip`
            : 'Waiting for data';
        ctx.setText('#summary-weather', weatherSummary);

        const currentDetails = [];
        if (currentAvailable) currentDetails.push(`${Math.round(currentTemperature)}°C`);
        currentDetails.push(`${currentPrecipitation}% precipitation`);
        if (humidityAvailable)
            currentDetails.push(`${Math.round(Number(weather.humidity_percent) || 0)}% humidity`);
        if (weather.current_temperature_is_forecast)
            currentDetails.push('nearest-hour forecast');
        ctx.setText('#status-weather-current', currentAvailable
            ? currentDetails.join(' · ')
            : 'Waiting for data');
        const weatherPanels = slots.filter(slot => String(slot.kind || '') !== 'room');
        ctx.setText('#status-weather-forecast', weatherPanels.length
            ? weatherPanels.map(slot => {
                const temperature = Number(slot.temperature_c);
                const available = slot.temperature_available === undefined
                    ? Number.isFinite(temperature)
                    : Boolean(slot.temperature_available);
                const reading = available ? `${Math.round(temperature)}°C` : '--°C';
                return `${slot.label || ''}${slot.date_label ? ` ${slot.date_label}` : ''} ${reading} ${weatherIconName(slot.icon)}`;
            }).join(' · ')
            : 'No outside or forecast panels selected');
        previewDelayMs = 250;
    };

    ctx.status.subscribe(render);
    ctx.on('click', '[data-clock-action]', async (_, button) => {
        try {
            await ctx.clock(button.dataset.clockAction, {}, button);
            await refreshPreview();
        } catch (_) {}
    });
    ctx.on('click', '#play-first-song', async (_, button) => {
        let music;
        try {
            music = await ctx.json('/api/v1/assets/music');
        } catch (error) {
            ctx.notice(error.message || 'Music library could not be loaded', 'warn', 3000);
            return;
        }

        const track = music.tracks?.[0];
        const file = track?.file || '';
        if (!file) {
            ctx.notice('Add music on the Music page first.', 'warn', 3000);
            return;
        }

        const label = track?.display || track?.title || file;
        try {
            await ctx.clock('play-music', {file}, button, {
                busyText: `Starting ${label}...`,
                done: `Playing ${label}`,
                errorText: `${label} could not be played`
            });
            await refreshPreview();
        } catch (_) {}
    });

    await Promise.allSettled([ctx.status.refresh(), refreshPreview()]);

    const statusTimer = window.setInterval(() => {
        ctx.status.refresh().catch(() => {});
    }, 5000);
    const schedulePreview = () => {
        if (ctx.signal.aborted) return;
        previewTimer = window.setTimeout(async () => {
            await refreshPreview();
            schedulePreview();
        }, previewDelayMs);
    };
    schedulePreview();
    ctx.signal.addEventListener('abort', () => {
        window.clearInterval(statusTimer);
        window.clearTimeout(previewTimer);
    }, {once: true});
}
