export async function mount(ctx) {
    let timer = 0;
    let localName = 'this clock';

    const schedule = () => {
        window.clearTimeout(timer);
        if (!ctx.signal.aborted) timer = window.setTimeout(refresh, 4000);
    };

    const deviceCard = device => {
        const connected = Boolean(device.connected);
        const trusted = Boolean(device.trusted);
        const nowPlaying = device?.media?.playing
            ? [device.media.title, device.media.artist].filter(Boolean).join(' - ')
            : '';
        return `
            <div class="mini-card">
                <div>
                    <div class="font-name">${ctx.html(device.name || device.address)}</div>
                    <div class="small muted">${ctx.html(device.address || '')}${trusted ? ' · Trusted' : ''}</div>
                    ${nowPlaying ? `<div class="small"><strong>Playing</strong> ${ctx.html(nowPlaying)}</div>` : ''}
                </div>
                <div class="button-row">
                    ${connected
                        ? '<span class="badge ok">Connected</span>'
                        : '<span class="badge">Paired</span>'}
                    ${connected
                        ? `<button class="btn alt small-btn" type="button" data-bt-action="disconnect" data-bt-address="${ctx.html(device.address)}">Disconnect</button>`
                        : `<button class="btn ok small-btn" type="button" data-bt-action="connect" data-bt-address="${ctx.html(device.address)}">Connect</button>`}
                    <button class="btn danger small-btn" type="button" data-bt-action="forget" data-bt-address="${ctx.html(device.address)}" data-bt-name="${ctx.html(device.name || device.address)}">Forget</button>
                </div>
            </div>`;
    };

    const render = state => {
        const available = Boolean(state?.available);
        const devices = Array.isArray(state?.devices) ? state.devices : [];
        const connected = devices.filter(device => device.connected);
        localName = state?.name || 'this clock';
        ctx.setText('#bt-local-name', localName);
        ctx.setText('#bt-pair-name', localName);
        ctx.setText('#bt-adapter', available ? (state.powered ? 'Ready' : 'Powered off') : 'Unavailable');
        const passkey = String(state?.pairing_passkey || '').trim();
        ctx.setText('#bt-pairing', available
            ? (passkey ? `Confirm ${passkey}` : (state.discoverable ? 'Discoverable' : 'Off'))
            : 'Unavailable');
        ctx.setText('#bt-connected', connected.length
            ? connected.map(device => device.name || device.address).join(', ')
            : 'None');
        const holder = ctx.$('#bt-devices');
        if (holder) holder.innerHTML = devices.length
            ? devices.map(deviceCard).join('')
            : `<div class="empty-state">${available ? 'No paired Bluetooth devices.' : ctx.html(state?.error || 'Bluetooth controller unavailable.')}</div>`;
        const start = ctx.$('#bt-pair-start');
        const stop = ctx.$('#bt-pair-stop');
        if (start) start.disabled = !available || Boolean(state.discoverable);
        if (stop) stop.disabled = !available || !state.discoverable;
    };

    async function refresh() {
        try {
            const state = await ctx.json('/api/v1/bluetooth', {signal: ctx.signal});
            render(state);
        } catch (error) {
            if (!ctx.signal.aborted) {
                render({available: false, devices: [], error: error.message || 'Bluetooth status unavailable'});
            }
        } finally {
            schedule();
        }
    }

    ctx.on('click', '#bt-pair-start', async (_event, button) => {
        try {
            await ctx.update('/api/v1/bluetooth/pairing', {
                method: 'POST',
                body: new URLSearchParams({enabled: '1'}),
                button,
                busyText: 'Starting pairing...',
                done: `${localName} is discoverable for 120 seconds`,
                errorText: 'Pairing mode could not start',
                refreshStatus: false
            });
            await refresh();
        } catch (_) {}
    });

    ctx.on('click', '#bt-pair-stop', async (_event, button) => {
        try {
            await ctx.update('/api/v1/bluetooth/pairing', {
                method: 'POST',
                body: new URLSearchParams({enabled: '0'}),
                button,
                busyText: 'Stopping pairing...',
                done: 'Pairing mode stopped',
                errorText: 'Pairing mode could not stop',
                refreshStatus: false
            });
            await refresh();
        } catch (_) {}
    });

    ctx.on('click', '[data-bt-action]', async (_event, button) => {
        const action = button.dataset.btAction;
        const address = button.dataset.btAddress;
        if (!action || !address) return;
        if (action === 'forget' && !confirm(`Forget ${button.dataset.btName || address}?`)) return;
        const labels = {
            connect: ['Connecting...', 'Bluetooth device connected'],
            disconnect: ['Disconnecting...', 'Bluetooth device disconnected'],
            forget: ['Forgetting...', 'Bluetooth device forgotten']
        };
        const [busyText, done] = labels[action] || ['Updating Bluetooth...', 'Bluetooth updated'];
        try {
            await ctx.update('/api/v1/bluetooth/device', {
                method: 'POST',
                body: new URLSearchParams({action, address}),
                button,
                busyText,
                done,
                errorText: 'Bluetooth device action failed',
                refreshStatus: false
            });
            await refresh();
        } catch (_) {}
    });

    ctx.signal.addEventListener('abort', () => window.clearTimeout(timer), {once: true});
    await refresh();
    return {refresh};
}
