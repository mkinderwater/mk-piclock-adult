import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';

const source = await readFile(new URL('../web/modules/display/module.js', import.meta.url), 'utf8');
const moduleUrl = `data:text/javascript;base64,${Buffer.from(source).toString('base64')}`;
const {mount} = await import(moduleUrl);

globalThis.window = globalThis;
globalThis.document = {
    documentElement: {style: {setProperty() {}}},
    fonts: {add() {}, delete() {}}
};

const nodes = new Map();
const node = selector => {
    if (!nodes.has(selector)) nodes.set(selector, {value: '', innerHTML: '', textContent: '', className: ''});
    return nodes.get(selector);
};
const controller = new AbortController();
const systemKey = 'system:0123456789abcdef';
let status = {
    clock_name: 'Adult Clock',
    bedtime_enabled: false,
    bedtime_start_hour: 21,
    bedtime_start_min: 0,
    bedtime_end_hour: 7,
    bedtime_end_min: 0,
    bedtime_dim_percent: 35,
    clock_24h_mode: false,
    oled_color: 'green',
    oled_font_file: '',
    oled_font: 0,
    oled_font_size: 48,
    inside_font_file: systemKey
};

const ctx = {
    signal: controller.signal,
    $: node,
    on() {},
    html(value) {
        return String(value ?? '')
            .replace(/&/g, '&amp;')
            .replace(/</g, '&lt;')
            .replace(/>/g, '&gt;')
            .replace(/"/g, '&quot;')
            .replace(/'/g, '&#39;');
    },
    setValue(selector, value) { node(selector).value = value == null ? '' : String(value); },
    setText(selector, value) { node(selector).textContent = value == null ? '' : String(value); },
    timeValue(hour, minute) { return `${hour}:${String(minute).padStart(2, '0')}`; },
    status: {
        get() { return status; },
        async refresh() { return status; }
    },
    async json(url) {
        assert.equal(url, '/api/v1/assets/fonts');
        return {
            selected: '',
            builtin: 0,
            font_size: 48,
            default_system_key: systemKey,
            builtin_fonts: [
                {id: 0, name: 'Seven Segment'},
                {id: 4, name: 'Automatic font detection'}
            ],
            system_fonts: [{key: systemKey, name: 'DejaVu Sans Mono', file: 'DejaVuSansMono.ttf'}],
            uploaded_fonts: ['inside.ttf']
        };
    },
    async binary() { return new ArrayBuffer(0); },
    async update() {},
    async clock() {},
    notice() {}
};

const mounted = await mount(ctx);
assert.match(node('#clock-font').innerHTML, /Seven Segment/);
assert.match(node('#inside-font').innerHTML, /Same as clock font/);
assert.match(node('#inside-font').innerHTML, /DejaVu Sans Mono/);
assert.match(node('#inside-font').innerHTML, /inside\.ttf/);
assert.equal(node('#inside-font').value, systemKey);
assert.equal(node('#inside-font-file').value, systemKey);

status = {...status, inside_font_file: ''};
await mounted.refresh();
assert.equal(node('#inside-font').value, 'clock');
assert.equal(node('#inside-font-file').value, '');

controller.abort();
console.log('display font selector tests passed');
