export async function mount(ctx) {
    let fonts = null;
    const previewFonts = new Map();
    const fontChoices = new Map();
    const fontBlobUrls = new Set();
    let brightnessTimer = 0;
    let brightnessPending = null;
    let brightnessSending = false;

    const previewOledColour = value => {
        const themes = {
            yellow: ['#ffd84a', 'rgba(255,216,74,.38)', 'rgba(255,216,74,.35)', 'rgba(255,216,74,.14)', 'rgba(255,216,74,.32)'],
            green: ['#9dff76', 'rgba(157,255,118,.38)', 'rgba(157,255,118,.35)', 'rgba(157,255,118,.14)', 'rgba(157,255,118,.32)'],
            white: ['#f5f7ff', 'rgba(245,247,255,.38)', 'rgba(245,247,255,.35)', 'rgba(245,247,255,.14)', 'rgba(245,247,255,.32)']
        };
        const selected = themes[String(value || '').toLowerCase()] || themes.green;
        const style = document.documentElement.style;
        ['--oled-color', '--oled-color-faint', '--oled-color-border', '--oled-color-divider', '--oled-color-glow']
            .forEach((name, index) => style.setProperty(name, selected[index]));
    };

    const setBrightnessValue = value => {
        const percent = Math.max(0, Math.min(100, Number.parseInt(value ?? 35, 10) || 0));
        ctx.setValue('#bedtime-dim-percent', percent);
        ctx.setText('#bedtime-dim-value', `${percent}%`);
        return percent;
    };

    const applyStatus = status => {
        if (!status) return;
        const start = ctx.timeValue(status.bedtime_start_hour ?? 21, status.bedtime_start_min ?? 0);
        const end = ctx.timeValue(status.bedtime_end_hour ?? 7, status.bedtime_end_min ?? 0);
        const values = {
            '#clock-name': status.clock_name,
            '#bedtime-enabled': status.bedtime_enabled ? 1 : 0,
            '#bedtime-start': start,
            '#bedtime-end': end,
            '#clock-24h-mode': status.clock_24h_mode ? 1 : 0,
            '#oled-color': status.oled_color || 'green',
            '#oled-font-file': status.oled_font_file,
            '#oled-font': status.oled_font ?? 0,
            '#oled-font-size': status.oled_font_size ?? 48,
            '#inside-font-file': status.inside_font_file || ''
        };
        Object.entries(values).forEach(([selector, value]) => ctx.setValue(selector, value));
        setBrightnessValue(status.bedtime_dim_percent ?? 35);
        previewOledColour(status.oled_color || 'green');
    };

    const sendBrightnessPreview = async percent => {
        brightnessPending = percent;
        if (brightnessSending) return;
        brightnessSending = true;
        try {
            while (brightnessPending !== null && !ctx.signal.aborted) {
                const next = brightnessPending;
                brightnessPending = null;
                await ctx.json('/api/v1/display/brightness-preview', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/x-www-form-urlencoded', 'Accept': 'application/json'},
                    body: new URLSearchParams({brightness_percent: next, hold_seconds: 8, format: 'json'}),
                    signal: ctx.signal
                });
            }
        } catch (error) {
            if (!ctx.signal.aborted) ctx.notice(error.message || 'Brightness preview failed', 'warn', 2500);
        } finally {
            brightnessSending = false;
        }
    };

    const queueBrightnessPreview = percent => {
        window.clearTimeout(brightnessTimer);
        brightnessTimer = window.setTimeout(() => sendBrightnessPreview(percent), 90);
    };

    const fontFamily = value => `MKFont_${String(value).replace(/[^a-z0-9]/gi, '_')}`;
    const registerFont = value => {
        const choice = fontChoices.get(value);
        const family = fontFamily(value);
        if (!choice || choice.kind === 'builtin' || previewFonts.has(value)) return family;
        previewFonts.set(value, {family, loading: true});
        const query = choice.kind === 'system'
            ? `key=${encodeURIComponent(choice.key)}`
            : `file=${encodeURIComponent(choice.file)}`;
        ctx.binary(`/api/v1/assets/fonts/file?${query}`, {signal: ctx.signal})
            .then(buffer => {
                if (ctx.signal.aborted) return;
                const mime = String(choice.file || '').toLowerCase().endsWith('.otf') ? 'font/otf' : 'font/ttf';
                const blobUrl = URL.createObjectURL(new Blob([buffer], {type: mime}));
                fontBlobUrls.add(blobUrl);
                const font = new FontFace(family, `url(${blobUrl})`);
                previewFonts.set(value, {family, font, blobUrl});
                document.fonts.add(font);
                return font.load();
            })
            .catch(() => {
                const entry = previewFonts.get(value);
                if (entry?.font) document.fonts.delete(entry.font);
                if (entry?.blobUrl) {
                    URL.revokeObjectURL(entry.blobUrl);
                    fontBlobUrls.delete(entry.blobUrl);
                }
                previewFonts.delete(value);
            });
        return family;
    };
    const sample = value => `<div class="font-sample-box" style="font-family:'${ctx.html(registerFont(value))}',sans-serif"><div>ABCDEFGHIJKLMNOPQRSTUVWXYZ</div><div>1234567890</div></div>`;

    const setFontChoice = value => {
        const choice = String(value || 'builtin:0');
        if (choice.startsWith('system:')) {
            ctx.setValue('#oled-font-file', choice);
            ctx.setValue('#oled-font', Number.isFinite(Number(fonts?.builtin)) ? Number(fonts.builtin) : 0);
        } else if (choice.startsWith('upload:')) {
            ctx.setValue('#oled-font-file', choice.slice(7));
            ctx.setValue('#oled-font', Number.isFinite(Number(fonts?.builtin)) ? Number(fonts.builtin) : 0);
        } else {
            const id = Number.parseInt(choice.slice(8), 10);
            ctx.setValue('#oled-font-file', '');
            ctx.setValue('#oled-font', Number.isFinite(id) ? id : 0);
        }
    };

    const setInsideFontChoice = value => {
        const choice = String(value || 'clock');
        if (choice.startsWith('system:')) ctx.setValue('#inside-font-file', choice);
        else if (choice.startsWith('upload:')) ctx.setValue('#inside-font-file', choice.slice(7));
        else ctx.setValue('#inside-font-file', '');
    };

    const updatePreview = () => {
        if (!fonts) return;
        const value = ctx.$('#clock-font')?.value || `builtin:${fonts.builtin ?? 0}`;
        const choice = fontChoices.get(value);
        if (!choice || choice.kind === 'missing') {
            ctx.$('#selected-font-preview').innerHTML = '<div class="empty-state">Selected font is unavailable.</div>';
            return;
        }
        if (choice.kind === 'builtin') {
            const builtinNote = choice.id === 4
                ? 'Select this to detect the preferred font automatically.'
                : 'Built-in OLED font preview is approximated in the browser.';
            ctx.$('#selected-font-preview').innerHTML = `<div class="font-preview-card"><div class="font-name">${ctx.html(choice.name)}</div><div class="font-sample-box mono"><div>ABCDEFGHIJKLMNOPQRSTUVWXYZ</div><div>1234567890</div></div><div class="small muted">${builtinNote}</div></div>`;
            return;
        }
        const note = choice.kind === 'system'
            ? `Linux font: ${ctx.html(choice.file)}`
            : 'Uploaded font';
        ctx.$('#selected-font-preview').innerHTML = `<div class="font-preview-card"><div class="font-name">${ctx.html(choice.name)}</div>${sample(value)}<div class="small muted">${note}</div></div>`;
    };

    const loadFonts = async () => {
        fonts = await ctx.json('/api/v1/assets/fonts');
        fontChoices.clear();

        const builtinFonts = [...(fonts.builtin_fonts || [])]
            .sort((a, b) => Number(a.id) - Number(b.id));
        const systemFonts = [...(fonts.system_fonts || [])].sort((a, b) =>
            String(a.name || a.file).localeCompare(String(b.name || b.file), undefined, {sensitivity: 'base'})
        ).map(font => ({
            ...font,
            name: font.key === fonts.default_system_key
                ? `${font.name || font.file} (Linux default)`
                : (font.name || font.file)
        }));
        const uploadedFonts = [...(fonts.uploaded_fonts || [])].sort((a, b) =>
            String(a).localeCompare(String(b), undefined, {sensitivity: 'base'})
        );

        builtinFonts.forEach(font => fontChoices.set(`builtin:${font.id}`, {
            kind: 'builtin', name: font.name, id: Number(font.id)
        }));
        systemFonts.forEach(font => fontChoices.set(font.key, {
            kind: 'system', name: font.name || font.file, key: font.key, file: font.file
        }));
        uploadedFonts.forEach(file => fontChoices.set(`upload:${file}`, {
            kind: 'upload', name: file, file
        }));

        let selectedChoice;
        if (String(fonts.selected || '').startsWith('system:')) selectedChoice = fonts.selected;
        else if (fonts.selected) selectedChoice = `upload:${fonts.selected}`;
        else selectedChoice = `builtin:${Number.isFinite(Number(fonts.builtin)) ? Number(fonts.builtin) : 0}`;

        if (!fontChoices.has(selectedChoice)) {
            fontChoices.set(selectedChoice, {kind: 'missing', name: 'Unavailable saved font'});
        }

        const builtinOptions = builtinFonts.map(font =>
            `<option value="builtin:${font.id}">${ctx.html(font.name)}</option>`
        ).join('');
        const systemOptions = systemFonts.map(font =>
            `<option value="${ctx.html(font.key)}">${ctx.html(font.name || font.file)}</option>`
        ).join('');
        const uploadedOptions = uploadedFonts.map(file =>
            `<option value="upload:${ctx.html(file)}">${ctx.html(file)}</option>`
        ).join('');
        const unavailableOption = fontChoices.get(selectedChoice)?.kind === 'missing'
            ? `<optgroup label="Unavailable"><option value="${ctx.html(selectedChoice)}">Unavailable saved font</option></optgroup>`
            : '';

        ctx.$('#clock-font').innerHTML = [
            `<optgroup label="Built-in OLED fonts">${builtinOptions}</optgroup>`,
            systemOptions ? `<optgroup label="Linux system fonts">${systemOptions}</optgroup>` : '',
            uploadedOptions ? `<optgroup label="Uploaded fonts">${uploadedOptions}</optgroup>` : '',
            unavailableOption
        ].join('');
        ctx.setValue('#clock-font', selectedChoice);
        ctx.setValue('#oled-font-size', fonts.font_size ?? 48);
        setFontChoice(selectedChoice);

        const savedInsideFont = String(ctx.$('#inside-font-file')?.value || '');
        let selectedInsideChoice = 'clock';
        if (savedInsideFont.startsWith('system:')) selectedInsideChoice = savedInsideFont;
        else if (savedInsideFont) selectedInsideChoice = `upload:${savedInsideFont}`;
        const insideMissing = selectedInsideChoice !== 'clock' && !fontChoices.has(selectedInsideChoice);
        ctx.$('#inside-font').innerHTML = [
            '<option value="clock">Same as clock font</option>',
            systemOptions ? `<optgroup label="Linux system fonts">${systemOptions}</optgroup>` : '',
            uploadedOptions ? `<optgroup label="Uploaded fonts">${uploadedOptions}</optgroup>` : '',
            insideMissing ? `<optgroup label="Unavailable"><option value="${ctx.html(selectedInsideChoice)}">Unavailable saved font</option></optgroup>` : ''
        ].join('');
        ctx.setValue('#inside-font', selectedInsideChoice);
        setInsideFontChoice(selectedInsideChoice);

        ctx.$('#delete-fonts').innerHTML = uploadedFonts.length
            ? uploadedFonts.map(file => `
                <div class="mini-card font-delete-row">
                    <div><div class="font-name">${ctx.html(file)}</div>${sample(`upload:${file}`)}</div>
                    <button class="btn danger small-btn" type="button" data-delete-font="${ctx.html(file)}">Delete</button>
                </div>`).join('')
            : '<div class="empty-state">No uploaded fonts.</div>';
        updatePreview();
    };

    const refresh = async () => {
        try {
            const status = await ctx.status.refresh();
            applyStatus(status || ctx.status.get());
        } catch (_) {}
        try {
            await loadFonts();
        } catch (_) {
            ctx.$('#clock-font').innerHTML = '<option>Could not load fonts</option>';
            ctx.$('#inside-font').innerHTML = '<option>Could not load fonts</option>';
        }
    };

    ctx.on('click', '[data-clock-action]', async (_, button) => {
        try { await ctx.clock(button.dataset.clockAction, {}, button); } catch (_) {}
    });
    ctx.on('input', '#bedtime-dim-percent', (_, slider) => {
        const percent = setBrightnessValue(slider.value);
        queueBrightnessPreview(percent);
    });
    ctx.on('change', '#bedtime-dim-percent', async (_, slider) => {
        const percent = setBrightnessValue(slider.value);
        window.clearTimeout(brightnessTimer);
        await sendBrightnessPreview(percent);
        try {
            await ctx.update('/api/v1/config/display', {
                method: 'POST',
                body: new URLSearchParams({bedtime_dim_percent: percent, format: 'json'}),
                busyText: `Saving bedtime brightness ${percent}%...`,
                done: `Bedtime brightness saved at ${percent}%`,
                errorText: 'Bedtime brightness could not be saved',
                refreshStatus: false
            });
        } catch (_) {}
    });
    ctx.on('change', '#oled-color', (_, select) => previewOledColour(select.value));
    ctx.on('change', '#clock-font', (_, select) => {
        setFontChoice(select.value);
        updatePreview();
    });
    ctx.on('change', '#inside-font', (_, select) => setInsideFontChoice(select.value));
    ctx.on('click', '[data-delete-font]', async (_, button) => {
        const name = button.dataset.deleteFont;
        if (!confirm(`Delete ${name}?`)) return;
        try {
            await ctx.update('/api/v1/assets/fonts/delete', {
                method: 'POST',
                body: new URLSearchParams({font: name, format: 'json'}),
                button,
                busyText: `Deleting ${name}...`,
                done: `Font deleted: ${name}`,
                errorText: `${name} could not be deleted`
            });
            await refresh();
        } catch (_) {}
    });

    ctx.signal.addEventListener('abort', () => {
        window.clearTimeout(brightnessTimer);
        previewFonts.forEach(entry => {
            if (entry.font) document.fonts.delete(entry.font);
        });
        previewFonts.clear();
        fontBlobUrls.forEach(url => URL.revokeObjectURL(url));
        fontBlobUrls.clear();
    }, {once: true});

    await refresh();
    return {refresh};
}
