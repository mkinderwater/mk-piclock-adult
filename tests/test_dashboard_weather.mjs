import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';

const source = await readFile(new URL('../web/modules/dashboard/module.js', import.meta.url), 'utf8');
const moduleUrl = `data:text/javascript;base64,${Buffer.from(source).toString('base64')}`;
const {mount} = await import(moduleUrl);

globalThis.window = globalThis;
const values = new Map();
const controller = new AbortController();
let renderStatus = null;

const baseStatus = {
    time: '9:04 AM',
    date: 'July 22, 2026',
    clock_name: 'Adult Clock',
    app_version: 'mk-clock-adult-1.2.62',
    uptime_seconds: 1,
    oled_ok: true,
    oled_color: 'green',
    display_mode: 'clock',
    oled_font_name: 'Built-in',
    room_sensor: {status: 'error'},
    weather: {
        current_temperature_c: 19,
        current_temperature_available: true,
        current_temperature_is_forecast: true,
        humidity_percent: 0,
        humidity_available: false,
        precipitation_probability_percent: 30,
        observed_at: 1,
        slots: [
            {kind: 'room', label: 'INSIDE', temperature_c: 19, temperature_available: false, icon: 2},
            {kind: 'outside', label: 'OUTSIDE', temperature_c: 19, temperature_available: true, icon: 2},
            {kind: 'forecast', label: '6PM', temperature_c: 17, temperature_available: true, icon: 3}
        ]
    }
};

const ctx = {
    signal: controller.signal,
    $: () => ({}),
    createOledPreview: () => ({setColour() {}, draw() {}}),
    setText: (selector, text) => values.set(selector, text),
    formatUptime: () => '1 second',
    timeValue: (hour, minute) => `${hour}:${String(minute).padStart(2, '0')}`,
    status: {
        subscribe(callback) { renderStatus = callback; },
        async refresh() { renderStatus(baseStatus); }
    },
    on() {},
    async binary() { return new Uint8Array(8192); },
    async json() { return {tracks: []}; },
    async clock() {},
    notice() {}
};

await mount(ctx);
assert.equal(values.get('#summary-sound'), 'Stopped');
assert.equal(values.get('#summary-weather'), '19°C · 30% precip');
assert.equal(values.get('#status-weather-current'), '19°C · 30% precipitation · nearest-hour forecast');
assert.equal(values.get('#status-weather-forecast'), 'OUTSIDE 19°C Partly cloudy · 6PM 17°C Cloudy');

renderStatus({
    ...baseStatus,
    weather: {
        ...baseStatus.weather,
        current_temperature_c: 0,
        current_temperature_is_forecast: false,
        humidity_percent: 54,
        humidity_available: true,
        precipitation_probability_percent: 0
    }
});
assert.equal(values.get('#summary-weather'), '0°C · 0% precip');
assert.equal(values.get('#status-weather-current'), '0°C · 0% precipitation · 54% humidity');

renderStatus({
    ...baseStatus,
    clock_24h_mode: 0,
    weather: {
        ...baseStatus.weather,
        slots: [
            {kind: 'room', label: 'INSIDE'},
            {
                kind: 'today',
                label: 'TODAY',
                low_temperature_c: 10,
                low_temperature_available: true,
                low_hour: 6,
                high_temperature_c: 24,
                high_temperature_available: true,
                high_hour: 16,
                precipitation_probability_percent: 40
            },
            {kind: 'forecast', label: '6PM', temperature_c: 17, temperature_available: true, icon: 3}
        ]
    }
});
assert.equal(values.get('#status-weather-forecast'),
    'TODAY Low 10°C at 6 AM, high 24°C at 4 PM, 40% precipitation · 6PM 17°C Cloudy');


renderStatus({
    ...baseStatus,
    weather: {
        ...baseStatus.weather,
        current_temperature_available: false,
        current_temperature_c: 0,
        slots: [
            {kind: 'room', label: 'INSIDE', temperature_c: 0, temperature_available: false, icon: 0},
            {kind: 'outside', label: 'OUTSIDE', temperature_c: 0, temperature_available: false, icon: 0},
            {kind: 'forecast', label: '6PM', temperature_c: 0, temperature_available: false, icon: 0}
        ]
    }
});
assert.equal(values.get('#summary-weather'), 'Waiting for data');
assert.equal(values.get('#status-weather-forecast'), 'OUTSIDE --°C Unknown · 6PM --°C Unknown');

renderStatus({...baseStatus, audio_playing: true, audio_title: 'Morning Song'});
assert.equal(values.get('#summary-sound'), 'Playing Morning Song');

renderStatus({...baseStatus, alarm_active: true, alarm_volume_percent: 65});
assert.equal(values.get('#summary-sound'), 'Alarm playing · 65%');

controller.abort();
console.log('dashboard weather tests passed');
