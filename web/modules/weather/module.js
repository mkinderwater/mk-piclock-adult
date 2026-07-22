const SOURCE_ENDPOINT = '/api/v1/config/weather-source';
const ACTIVITY_ENDPOINT = '/api/v1/weather/activity';
const FRAMES_ENDPOINT = '/api/v1/config/weather-frames';
const STATUS_ENDPOINT = '/api/v1/status';
const DISPLAY_CONFIG_ENDPOINT = '/api/v1/config/display';
const CALGARY_URL = 'https://api.weather.gc.ca/collections/citypageweather-realtime/items/ab-52?f=json';

export async function mount(ctx) {
    const input = ctx.$('#weather-source-url');
    const save = ctx.$('#weather-source-save');
    const calgary = ctx.$('#weather-source-calgary');
    const revert = ctx.$('#weather-source-revert');
    const status = ctx.$('#weather-source-status');
    const roomSensorIcon = ctx.$('#room-sensor-icon');
    const roomSensorStatus = ctx.$('#room-sensor-status');
    const roomSensorTemperature = ctx.$('#room-sensor-temperature');
    const roomSensorHumidity = ctx.$('#room-sensor-humidity');
    const roomSensorDevice = ctx.$('#room-sensor-device');
    const roomSensorMeasured = ctx.$('#room-sensor-measured');
    const roomSensorError = ctx.$('#room-sensor-error');
    const activityRefresh = ctx.$('#weather-activity-refresh');
    const activitySummary = ctx.$('#weather-activity-summary');
    const activityList = ctx.$('#weather-activity-list');
    const framesSave = ctx.$('#weather-frames-save');
    const framesDefaults = ctx.$('#weather-frames-defaults');
    const framesStatus = ctx.$('#weather-frames-status');
    const warningChimeEnabled = ctx.$('#weather-warning-chime-enabled');
    const warningChimeBedtime = ctx.$('#weather-warning-chime-bedtime');
    const warningChimeSave = ctx.$('#weather-warning-chime-save');
    const warningChimeStatus = ctx.$('#weather-warning-chime-status');
    const frameControls = [1, 2, 3].map(index => ({
        mode: ctx.$(`#weather-frame-${index}-mode`),
        offset: ctx.$(`#weather-frame-${index}-offset`)
    }));
    let savedUrl = '';
    let dirty = false;
    let activityLoading = false;

    const setStatus = (text, kind = '') => {
        if (!status) return;
        status.textContent = text;
        status.className = `small no-margin ${kind}`.trim();
    };

    const setBusy = busy => {
        save.disabled = busy;
        calgary.disabled = busy;
        revert.disabled = busy || !dirty;
        input.disabled = busy;
    };

    const updateDirtyState = () => {
        dirty = input.value.trim() !== savedUrl;
        revert.disabled = !dirty;
        setStatus(dirty
            ? 'URL changed. Press Save URL & Refresh to apply it.'
            : 'This is the active weather source URL.');
    };

    const setFramesStatus = (text, kind = '') => {
        if (!framesStatus) return;
        framesStatus.textContent = text;
        framesStatus.className = `small no-margin ${kind}`.trim();
    };

    const setWarningChimeStatus = (text, kind = '') => {
        if (!warningChimeStatus) return;
        warningChimeStatus.textContent = text;
        warningChimeStatus.className = `small no-margin ${kind}`.trim();
    };

    const setWarningChimeBusy = busy => {
        warningChimeEnabled.disabled = busy;
        warningChimeBedtime.disabled = busy;
        warningChimeSave.disabled = busy;
    };

    const applyWarningChimeSettings = data => {
        warningChimeEnabled.value = data.weather_warning_chime_enabled === 0 ? '0' : '1';
        warningChimeBedtime.value = data.weather_warning_chime_during_bedtime === 1 ? '1' : '0';
    };

    const updateFrameControls = index => {
        const controls = frameControls[index];
        const mode = controls.mode.value;
        controls.offset.disabled = mode !== 'offset' || framesSave.disabled;
    };

    const setFramesBusy = busy => {
        framesSave.disabled = busy;
        framesDefaults.disabled = busy;
        frameControls.forEach((controls, index) => {
            controls.mode.disabled = busy;
            updateFrameControls(index);
        });
    };

    const applyFrames = data => {
        const defaults = [
            {mode: 'room', offset_hours: 1},
            {mode: 'offset', offset_hours: 3},
            {mode: 'offset', offset_hours: 6}
        ];
        [data.slot1 || {}, data.slot2 || {}, data.slot3 || {}].forEach((slot, index) => {
            const controls = frameControls[index];
            const fallback = defaults[index];
            controls.mode.value = ['room', 'outside'].includes(slot.mode) ? slot.mode : 'offset';
            controls.offset.value = String(slot.offset_hours ?? fallback.offset_hours);
            updateFrameControls(index);
        });
    };

    const responseJson = async response => {
        const text = await response.text();
        let data;
        try {
            data = text ? JSON.parse(text) : {};
        } catch (_) {
            throw new Error(`Clock API returned HTTP ${response.status}`);
        }
        if (!response.ok || data.ok === false) {
            throw new Error(data.error || `Clock API returned HTTP ${response.status}`);
        }
        return data;
    };

    const formatTime = value => {
        if (!value) return 'Time unavailable';
        const parsed = new Date(value);
        return Number.isNaN(parsed.getTime()) ? String(value) : parsed.toLocaleString();
    };

    const renderRoomSensor = data => {
        const room = data.room_sensor || {};
        const state = String(room.status || 'waiting');
        const hasReading = ['active', 'stale'].includes(state);
        roomSensorStatus.textContent = state === 'active'
            ? 'Active'
            : state === 'stale'
                ? 'Stale reading'
                : state === 'disabled'
                    ? 'Disabled'
                    : state === 'error'
                        ? 'Unavailable'
                        : 'Starting';
        roomSensorTemperature.textContent = hasReading
            ? `${Number(room.temperature_c).toFixed(1)}°C`
            : 'Unavailable';
        roomSensorHumidity.textContent = hasReading
            ? `${Number(room.humidity_percent).toFixed(1)}% RH`
            : 'Unavailable';
        roomSensorDevice.textContent = `${room.device || '/dev/i2c-1'} at ${room.address || '0x38'}`;
        roomSensorMeasured.textContent = room.measured_at
            ? new Date(Number(room.measured_at) * 1000).toLocaleString()
            : 'No successful reading';
        roomSensorError.textContent = room.error || '';
        roomSensorError.className = `small no-margin ${room.error ? 'warn-text' : ''}`.trim();
        roomSensorIcon.src = state === 'active'
            ? '/assets/icons/room-sensor-normal.png?v=mk-clock-adult-1.2.40'
            : state === 'stale'
                ? '/assets/icons/room-sensor-stale.png?v=mk-clock-adult-1.2.40'
                : state === 'error' || state === 'disabled'
                    ? '/assets/icons/room-sensor-error.png?v=mk-clock-adult-1.2.40'
                    : '/assets/icons/room-sensor-waiting.png?v=mk-clock-adult-1.2.40';
    };

    const loadRoomSensor = async () => {
        try {
            const data = await responseJson(await fetch(STATUS_ENDPOINT, {
                method: 'GET',
                headers: {'Accept': 'application/json'},
                cache: 'no-store',
                signal: ctx.signal
            }));
            if (!ctx.signal.aborted) renderRoomSensor(data);
        } catch (error) {
            if (!ctx.signal.aborted) {
                roomSensorStatus.textContent = 'Unavailable';
                roomSensorError.textContent = error.message || 'Room sensor status could not be loaded.';
                roomSensorError.className = 'small no-margin warn-text';
                roomSensorIcon.src = '/assets/icons/room-sensor-error.png?v=mk-clock-adult-1.2.40';
            }
        }
    };

    const renderActivity = data => {
        const latest = data.status || {};
        const entries = Array.isArray(data.activity?.entries)
            ? [...data.activity.entries].reverse().slice(0, 20)
            : [];

        const result = String(latest.result || 'pending');
        const latestSuccess = latest.ok === true || result === 'success' || result === 'success_with_warnings';
        activitySummary.textContent = latest.message || 'No weather update has run yet.';
        activitySummary.className = `small ${latestSuccess ? 'ok-text' : result === 'error' ? 'warn-text' : ''}`.trim();
        activityList.replaceChildren();

        if (!entries.length) {
            const empty = document.createElement('p');
            empty.className = 'small no-margin';
            empty.textContent = 'No weather activity has been recorded.';
            activityList.append(empty);
            return;
        }

        for (const entry of entries) {
            const item = document.createElement('div');
            const heading = document.createElement('p');
            const detail = document.createElement('p');
            const source = document.createElement('p');
            const icons = document.createElement('p');
            const divider = document.createElement('hr');
            const success = entry.ok === true || entry.result === 'success' || entry.result === 'success_with_warnings';

            heading.className = `no-margin ${success ? 'ok-text' : 'warn-text'}`;
            heading.textContent = `${success ? 'Successful' : 'Failed'} • ${formatTime(entry.run_at)}`;
            detail.className = 'small';
            detail.textContent = entry.message || (success ? 'Weather updated.' : 'Weather update failed.');
            source.className = 'small';
            source.textContent = entry.source_url
                ? `Source: ${entry.source_url}`
                : `Source file: ${entry.source_file || 'unavailable'}`;
            icons.className = 'small';
            icons.textContent = entry.icon_count != null
                ? `Pixel-art sprites: ${entry.icon_count}/3${entry.icon_codes ? ` (${entry.icon_codes})` : ''}${entry.icon_substitution_count ? `; ${entry.icon_substitution_count} unknown substitution(s)` : ''}`
                : 'Pixel-art sprites: not reported';

            item.append(heading, detail, source, icons, divider);
            activityList.append(item);
        }
    };

    const loadActivity = async () => {
        if (activityLoading || ctx.signal.aborted) return;
        activityLoading = true;
        activityRefresh.disabled = true;
        try {
            const data = await responseJson(await fetch(ACTIVITY_ENDPOINT, {
                method: 'GET',
                headers: {'Accept': 'application/json'},
                cache: 'no-store',
                signal: ctx.signal
            }));
            if (!ctx.signal.aborted) renderActivity(data);
        } catch (error) {
            if (!ctx.signal.aborted) {
                activitySummary.textContent = error.message || 'Weather activity could not be loaded.';
                activitySummary.className = 'small warn-text';
            }
        } finally {
            activityLoading = false;
            if (!ctx.signal.aborted) activityRefresh.disabled = false;
        }
    };

    const loadFrames = async () => {
        setFramesStatus('Loading weather panel settings.');
        try {
            const data = await responseJson(await fetch(FRAMES_ENDPOINT, {
                method: 'GET',
                headers: {'Accept': 'application/json'},
                cache: 'no-store',
                signal: ctx.signal
            }));
            if (ctx.signal.aborted) return;
            applyFrames(data);
            setFramesStatus('All three weather panels may be configured independently.');
        } catch (error) {
            if (!ctx.signal.aborted) {
                setFramesStatus(error.message || 'Weather panel settings could not be loaded.', 'warn-text');
            }
        }
    };

    const loadWarningChimeSettings = async () => {
        setWarningChimeStatus('Loading warning chime settings.');
        try {
            const data = await responseJson(await fetch(STATUS_ENDPOINT, {
                method: 'GET',
                headers: {'Accept': 'application/json'},
                cache: 'no-store',
                signal: ctx.signal
            }));
            if (ctx.signal.aborted) return;
            applyWarningChimeSettings(data);
            setWarningChimeStatus(data.weather_warning_chime_enabled === 0
                ? 'Weather warning chime is disabled.'
                : data.weather_warning_chime_during_bedtime === 1
                    ? 'Weather warning chime is enabled, including during bedtime.'
                    : 'Weather warning chime is enabled and silenced during bedtime.');
        } catch (error) {
            if (!ctx.signal.aborted) {
                setWarningChimeStatus(error.message || 'Warning chime settings could not be loaded.', 'warn-text');
            }
        }
    };

    const loadSource = async () => {
        setStatus('Loading weather source.');
        try {
            const data = await responseJson(await fetch(SOURCE_ENDPOINT, {
                method: 'GET',
                headers: {'Accept': 'application/json'},
                cache: 'no-store',
                signal: ctx.signal
            }));
            if (ctx.signal.aborted) return;
            savedUrl = String(data.url || data.default_url || CALGARY_URL);
            if (!dirty) input.value = savedUrl;
            updateDirtyState();
        } catch (error) {
            if (!ctx.signal.aborted) {
                setStatus(error.message || 'Weather source could not be loaded.', 'warn-text');
            }
        }
    };

    ctx.on('input', '#weather-source-url', updateDirtyState);

    ctx.on('change', '#weather-frame-1-mode', () => updateFrameControls(0));
    ctx.on('change', '#weather-frame-2-mode', () => updateFrameControls(1));
    ctx.on('change', '#weather-frame-3-mode', () => updateFrameControls(2));

    ctx.on('click', '#weather-frames-defaults', () => {
        applyFrames({
            slot1: {mode: 'room', offset_hours: 1},
            slot2: {mode: 'offset', offset_hours: 3},
            slot3: {mode: 'offset', offset_hours: 6}
        });
        setFramesStatus('ROOM, +3h and +6h selected. Press Save Panels & Refresh to apply them.');
    });

    ctx.on('submit', '#weather-frames-form', async (event, form) => {
        event.preventDefault();
        if (!form.reportValidity()) return;

        const body = new URLSearchParams();
        for (let index = 0; index < frameControls.length; index++) {
            const slot = index + 1;
            const controls = frameControls[index];
            const mode = controls.mode.value;
            const offset = Number.parseInt(controls.offset.value, 10);
            if (!['room', 'outside', 'offset'].includes(mode)) {
                setFramesStatus(`Panel ${slot} has an invalid source.`, 'warn-text');
                return;
            }
            if (mode === 'offset' && (!Number.isInteger(offset) || offset < 1 || offset > 48)) {
                setFramesStatus(`Panel ${slot} hours ahead must be between 1 and 48.`, 'warn-text');
                return;
            }
            const fallbackOffsets = [1, 3, 6];
            body.set(`slot${slot}_mode`, mode);
            body.set(`slot${slot}_offset_hours`, String(Number.isInteger(offset) ? offset : fallbackOffsets[index]));
        }

        setFramesBusy(true);
        setFramesStatus('Saving weather panel settings.');
        try {
            const data = await responseJson(await fetch(FRAMES_ENDPOINT, {
                method: 'POST',
                headers: {
                    'Accept': 'application/json',
                    'Content-Type': 'application/x-www-form-urlencoded'
                },
                body,
                signal: ctx.signal
            }));
            if (ctx.signal.aborted) return;
            applyFrames(data);
            setFramesStatus('Weather panels saved. Weather refresh requested.', 'ok-text');
            activitySummary.textContent = 'Weather refresh requested. Waiting for the result.';
            activitySummary.className = 'small';
            ctx.notice('Weather panels saved', 'ok', 1800);
            window.setTimeout(loadActivity, 1500);
        } catch (error) {
            if (!ctx.signal.aborted) {
                setFramesStatus(error.message || 'Weather panel settings could not be saved.', 'warn-text');
                ctx.notice(error.message || 'Weather panel settings could not be saved', 'warn', 3500);
            }
        } finally {
            if (!ctx.signal.aborted) setFramesBusy(false);
        }
    });

    ctx.on('submit', '#weather-warning-chime-form', async (event, form) => {
        event.preventDefault();
        if (!form.reportValidity()) return;

        const body = new URLSearchParams({
            weather_warning_chime_enabled: warningChimeEnabled.value,
            weather_warning_chime_during_bedtime: warningChimeBedtime.value
        });

        setWarningChimeBusy(true);
        setWarningChimeStatus('Saving warning chime settings.');
        try {
            await responseJson(await fetch(DISPLAY_CONFIG_ENDPOINT, {
                method: 'POST',
                headers: {
                    'Accept': 'application/json',
                    'Content-Type': 'application/x-www-form-urlencoded'
                },
                body,
                signal: ctx.signal
            }));
            if (ctx.signal.aborted) return;
            setWarningChimeStatus(warningChimeEnabled.value === '0'
                ? 'Weather warning chime disabled.'
                : warningChimeBedtime.value === '1'
                    ? 'Weather warning chime enabled, including during bedtime.'
                    : 'Weather warning chime enabled and silenced during bedtime.', 'ok-text');
            ctx.notice('Warning chime settings saved', 'ok', 1800);
        } catch (error) {
            if (!ctx.signal.aborted) {
                setWarningChimeStatus(error.message || 'Warning chime settings could not be saved.', 'warn-text');
                ctx.notice(error.message || 'Warning chime settings could not be saved', 'warn', 3500);
            }
        } finally {
            if (!ctx.signal.aborted) setWarningChimeBusy(false);
        }
    });

    ctx.on('submit', '#weather-source-form', async (event, form) => {
        event.preventDefault();
        if (!form.reportValidity()) return;

        const requestedUrl = input.value.trim();
        setBusy(true);
        setStatus('Saving weather source URL.');
        try {
            const body = new URLSearchParams({url: requestedUrl});
            const data = await responseJson(await fetch(SOURCE_ENDPOINT, {
                method: 'POST',
                headers: {
                    'Accept': 'application/json',
                    'Content-Type': 'application/x-www-form-urlencoded'
                },
                body,
                signal: ctx.signal
            }));
            if (ctx.signal.aborted) return;

            savedUrl = String(data.url || requestedUrl);
            input.value = savedUrl;
            dirty = false;
            setStatus(data.changed === false
                ? 'URL was already active. Weather refresh requested.'
                : 'URL saved. Weather refresh requested.', 'ok-text');
            activitySummary.textContent = 'Weather refresh requested. Waiting for the result.';
            activitySummary.className = 'small';
            ctx.notice('Weather URL saved', 'ok', 1800);
            window.setTimeout(loadActivity, 1500);
        } catch (error) {
            if (!ctx.signal.aborted) {
                setStatus(error.message || 'Weather URL could not be saved.', 'warn-text');
                ctx.notice(error.message || 'Weather URL could not be saved', 'warn', 3500);
            }
        } finally {
            if (!ctx.signal.aborted) setBusy(false);
        }
    });

    ctx.on('click', '#weather-source-calgary', () => {
        input.value = CALGARY_URL;
        input.focus();
        updateDirtyState();
    });

    ctx.on('click', '#weather-source-revert', () => {
        input.value = savedUrl;
        input.focus();
        updateDirtyState();
    });

    ctx.on('click', '#weather-activity-refresh', loadActivity);

    const timer = window.setInterval(loadActivity, 10000);
    const roomSensorTimer = window.setInterval(loadRoomSensor, 5000);
    ctx.signal.addEventListener('abort', () => {
        window.clearInterval(timer);
        window.clearInterval(roomSensorTimer);
    }, {once: true});

    setBusy(false);
    setFramesBusy(false);
    setWarningChimeBusy(false);
    await Promise.all([loadSource(), loadFrames(), loadWarningChimeSettings(), loadRoomSensor(), loadActivity()]);
    return {
        refresh: async () => {
            if (!dirty) await loadSource();
            await Promise.all([loadFrames(), loadWarningChimeSettings(), loadRoomSensor(), loadActivity()]);
        }
    };
}
