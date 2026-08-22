import {audioTrackFacts, formatAudioBytes} from '/assets/js/audio-library.js?v=mk-clock-adult-2.3.54-bpi-m2-zero-r1';

const MAX_PODCAST_UPLOAD_FILES = 14;
const MAX_PODCAST_FILE_BYTES = 256 * 1024 * 1024;
const MAX_PODCAST_BATCH_BYTES = 500 * 1024 * 1024;

export async function mount(ctx) {
    const renderStatus = status => {
        if (!status) return;
        const playingPodcast = Boolean(status.audio_playing && status.audio_kind === 'podcast');
        const title = status.audio_title || status.audio_file || 'Podcast';
        ctx.setText('#podcast-status', playingPodcast ? 'Playing' : 'None');
        ctx.setText('#podcast-current', playingPodcast ? title : 'None');
        ctx.setValue('#podcast-volume', status.podcast_volume ?? 30);
        ctx.setText('#podcast-volume-value', `${status.podcast_volume ?? 30}%`);
        ctx.setValue('#podcast-touch-enabled', status.podcast_touch_enabled ? '1' : '0');
    };

    const refreshSummary = async () => {
        try {
            const data = await ctx.json('/api/v1/assets/podcasts/summary');
            const total = Number(data.total || 0);
            const unplayed = Number(data.unplayed_count || 0);
            ctx.setText('#podcast-count', total ? `${unplayed} of ${total}` : 'None');
            return data;
        } catch (_) {
            ctx.setText('#podcast-count', 'Unavailable');
            return null;
        }
    };

    const refreshLibrary = async () => {
        const openPodcastDetails = new Set(
            Array.from(ctx.$$('#podcast-list details.file-details[open][data-podcast-details]') || [])
                .map(details => details.dataset.podcastDetails)
                .filter(Boolean)
        );
        try {
            const data = await ctx.json('/api/v1/assets/podcasts');
            const tracks = data.tracks || [];
            ctx.setValue('#podcast-volume', data.podcast_volume ?? 30);
            ctx.setText('#podcast-volume-value', `${data.podcast_volume ?? 30}%`);
            ctx.setValue('#podcast-touch-enabled', data.podcast_touch_enabled ? '1' : '0');
            ctx.$('#podcast-list').innerHTML = tracks.length
                ? tracks.map(track => {
                    const facts = audioTrackFacts(track);
                    const details = [
                        track.album ? `Series: ${track.album}` : '',
                        track.year ? `Year: ${track.year}` : '',
                        track.genre ? `Genre: ${track.genre}` : ''
                    ].filter(Boolean);
                    return `
                    <div class="mini-card media-library-card">
                        <div class="media-library-details">
                            <div class="font-name">${ctx.html(track.title || track.file)}</div>
                            ${track.artist ? `<div class="media-library-artist">${ctx.html(track.artist)}</div>` : ''}
                            ${(details.length || facts.length) ? `<details class="file-details" data-podcast-details="${ctx.html(track.file)}"><summary>Podcast details</summary>
                                ${details.length ? `<div class="media-library-tags">${details.map(value => `<span>${ctx.html(value)}</span>`).join('')}</div>` : ''}
                                ${facts.length ? `<div class="media-library-facts">${facts.map(value => `<span>${ctx.html(value)}</span>`).join('')}</div>` : ''}
                                <div class="small muted media-library-file">${ctx.html(track.file)}${track.id3 ? ' · ID3 tags' : ''}</div>
                            </details>` : `<div class="small muted media-library-file">${ctx.html(track.file)}</div>`}
                        </div>
                        <div class="media-library-actions">
                            <button class="btn ok small-btn" type="button" data-podcast-play="${ctx.html(track.file)}" data-podcast-label="${ctx.html(track.title || track.file)}">Play</button>
                            <button class="btn small-btn" type="button" data-podcast-stop>Stop</button>
                            <button class="btn danger small-btn" type="button" data-podcast-delete="${ctx.html(track.file)}" data-podcast-label="${ctx.html(track.title || track.file)}">Delete</button>
                        </div>
                    </div>`;
                }).join('')
                : '<div class="empty-state">No podcast MP3 files yet.</div>';
            Array.from(ctx.$$('#podcast-list details.file-details[data-podcast-details]') || []).forEach(details => {
                if (openPodcastDetails.has(details.dataset.podcastDetails)) details.open = true;
            });
        } catch (_) {
            ctx.$('#podcast-list').innerHTML = '<div class="empty-state error-state">Could not load podcasts.</div>';
            const count = ctx.$('#podcast-count');
            if (count && /Loading/i.test(count.textContent || '')) count.textContent = 'Unavailable';
        }
    };


    let importTimer = null;
    const refreshImport = async (start = false) => {
        try {
            const data = await ctx.json(`/api/v1/assets/podcasts/import${start ? '?start=1' : ''}`);
            const active = Boolean(data.active);
            const total = Number(data.total || 0);
            const processed = Number(data.processed || 0);
            const failed = Number(data.failed || 0);
            const waiting = Number(data.waiting || 0);
            const processButton = ctx.$('#process-podcast-import');
            const importCard = ctx.$('#podcast-import-card');
            const importBar = ctx.$('#podcast-import-bar');
            const currentProgress = active ? Math.max(0, Math.min(100, Number(data.progress || 0))) : 0;
            const completed = processed + failed;
            const overallProgress = total > 0
                ? Math.max(0, Math.min(100, ((completed + (active ? currentProgress / 100 : 0)) / total) * 100))
                : 0;
            if (importCard) {
                importCard.classList.toggle('hidden', total === 0 && waiting === 0 && failed === 0 && !active);
                importCard.classList.remove('processing', 'complete', 'failed');
                if (active) importCard.classList.add('processing');
                else if (failed) importCard.classList.add('failed');
                else if (total > 0 && waiting === 0) importCard.classList.add('complete');
            }
            ctx.setText('#podcast-import-status', active ? 'Processing podcasts' : (waiting ? 'Podcasts ready' : (failed ? 'Podcast processing complete' : 'Podcast uploads')));
            ctx.setText('#podcast-import-position', active
                ? `Processing ${Math.min(completed + 1, total)} of ${total}`
                : (waiting ? `${waiting} ready` : (total ? `${processed} of ${total} complete` : 'No uploads')));
            if (importBar) importBar.value = overallProgress;
            ctx.setText('#podcast-import-progress', total ? `${Math.round(overallProgress)}% overall` : '0%');
            const current = data.current_file
                ? `${data.current_file} · ${currentProgress}%`
                : (waiting ? `${waiting} podcast${waiting === 1 ? '' : 's'} waiting to be processed.` : 'No podcast processing in progress.');
            ctx.setText('#podcast-import-current', current);
            const failures = Array.isArray(data.failures) ? data.failures : [];
            const failureList = ctx.$('#podcast-import-failures');
            if (failureList) {
                failureList.classList.toggle('hidden', failures.length === 0);
                failureList.innerHTML = failures.map(item =>
                    `<div class="small job-error"><strong>${ctx.html(item.file || 'Unknown file')}</strong> · ${ctx.html(item.error || 'Processing failed')}</div>`
                ).join('');
            }
            if (!active && total > 0 && waiting === 0) {
                ctx.setText('#podcast-import-position', failed
                    ? `${processed} processed · ${failed} failed · ${total} total`
                    : `${processed} processed · ${total} total`);
            }
            if (processButton) {
                processButton.hidden = active || waiting === 0;
                processButton.disabled = active || waiting === 0;
                processButton.textContent = `Process ${waiting} podcast${waiting === 1 ? '' : 's'}`;
            }
            if (active) {
                clearTimeout(importTimer);
                importTimer = setTimeout(async () => { await refreshImport(); await refreshSummary(); await refreshLibrary(); }, 2000);
            }
        } catch (_) {
            ctx.setText('#podcast-import-status', 'Unavailable');
        }
    };

    const selectedFiles = () => Array.from(ctx.$('#podcast-files')?.files || []);
    const updateSelectionSummary = () => {
        const files = selectedFiles();
        const summary = ctx.$('#podcast-selection-summary');
        if (!summary) return;
        if (!files.length) {
            summary.textContent = 'No podcasts selected.';
            return;
        }
        const totalBytes = files.reduce((sum, file) => sum + (Number(file.size) || 0), 0);
        const total = formatAudioBytes(totalBytes);
        summary.textContent = `${files.length} podcast${files.length === 1 ? '' : 's'} selected${total ? ` · ${total}` : ''}.`;
    };

    ctx.on('change', '#podcast-files', updateSelectionSummary);
    ctx.on('submit', '#podcast-upload-form', async (event, form) => {
        event.preventDefault();
        const button = event.submitter || form.querySelector('[type="submit"]');
        const files = selectedFiles();
        if (!files.length) {
            ctx.notice('Select one or more MP3 files.', 'warn', 3000);
            return;
        }
        if (files.length > MAX_PODCAST_UPLOAD_FILES) {
            ctx.notice(`Select ${MAX_PODCAST_UPLOAD_FILES} MP3 files or fewer.`, 'warn', 3000);
            return;
        }
        if (files.some(file => Number(file.size) > MAX_PODCAST_FILE_BYTES)) {
            ctx.notice('Each podcast must be 256 MB or smaller.', 'warn', 4000);
            return;
        }
        const totalBytes = files.reduce((sum, file) => sum + (Number(file.size) || 0), 0);
        if (totalBytes > MAX_PODCAST_BATCH_BYTES) {
            ctx.notice('This batch is too large. Upload fewer podcasts at once.', 'warn', 4000);
            return;
        }
        try {
            const response = await ctx.update(form.action, {
                method: 'POST',
                body: new FormData(form),
                button,
                busyText: 'Uploading podcasts...',
                done: files.length === 1 ? 'Podcast added' : 'Podcasts added',
                errorText: 'Podcasts could not be added',
                refreshStatus: false
            });
            const result = await response.json();
            ctx.$('#podcast-files').value = '';
            updateSelectionSummary();
            ctx.notice(`${result.uploaded || files.length} podcast${(result.uploaded || files.length) === 1 ? '' : 's'} staged for processing.`, 'ok', 3000);
            await refreshImport();
        } catch (_) {}
    });


    ctx.on('click', '#scan-podcast-uploads', async (_, button) => {
        try {
            button.disabled = true;
            ctx.setText('#podcast-import-status', 'Scanning uploads');
            const importCard = ctx.$('#podcast-import-card');
            if (importCard) importCard.classList.remove('hidden');
            ctx.setText('#podcast-import-position', 'Scanning');
            ctx.setText('#podcast-import-current', 'Checking the podcast upload directory...');
            const data = await ctx.json('/api/v1/assets/podcasts/import?scan=1');
            const waiting = Number(data.waiting || 0);
            await refreshImport();
            ctx.notice(waiting ? `${waiting} podcast${waiting === 1 ? '' : 's'} ready to process.` : 'No podcast uploads found.', waiting ? 'ok' : 'warn', 3000);
        } catch (_) {
            ctx.setText('#podcast-import-status', 'Unavailable');
            ctx.notice('Podcast upload directory could not be scanned.', 'error', 4000);
        } finally {
            button.disabled = false;
        }
    });

    ctx.on('click', '#process-podcast-import', async (_, button) => {
        try {
            button.disabled = true;
            button.hidden = true;
            ctx.setText('#podcast-import-status', 'Starting podcast processing');
            ctx.setText('#podcast-import-position', 'Starting');
            const data = await ctx.json('/api/v1/assets/podcasts/import?start=1');
            if (data.active || Number(data.waiting || 0) > 0 || Number(data.total || 0) > 0)
                ctx.notice('Podcast processing started.', 'ok', 2500);
            clearTimeout(importTimer);
            importTimer = setTimeout(async () => { await refreshImport(); await refreshSummary(); await refreshLibrary(); }, 250);
        } catch (_) {
            button.disabled = false;
            button.hidden = false;
            ctx.setText('#podcast-import-status', 'Podcasts ready');
            ctx.notice('Podcast processing could not be started.', 'error', 4000);
        }
    });

    ctx.on('click', '[data-podcast-play]', async (_, button) => {
        const file = button.dataset.podcastPlay;
        const label = button.dataset.podcastLabel || file;
        try {
            await ctx.clock('play-podcast', {file}, button, {
                busyText: `Starting ${label}...`,
                done: `Playing ${label}`,
                errorText: `${label} could not be played`
            });
            renderStatus(ctx.status.get());
            await refreshSummary(); await refreshLibrary();
        } catch (_) {}
    });

    ctx.on('click', '[data-podcast-stop]', async (_, button) => {
        try {
            await ctx.clock('stop', {}, button);
            renderStatus(ctx.status.get());
        } catch (_) {}
    });

    ctx.on('click', '[data-podcast-delete]', async (_, button) => {
        const file = button.dataset.podcastDelete;
        const label = button.dataset.podcastLabel || file;
        if (!confirm(`Delete ${label}?`)) return;
        try {
            await ctx.update('/api/v1/assets/podcasts/delete', {
                method: 'POST',
                body: new URLSearchParams({file, format: 'json'}),
                button,
                busyText: `Deleting ${label}...`,
                done: `${label} deleted`,
                errorText: `${label} could not be deleted`
            });
            await refreshSummary(); await refreshLibrary();
        } catch (_) {}
    });


    ctx.on('click', '#reset-podcast-history', async (_, button) => {
        if (!confirm('Reset podcast play history? All podcasts will become eligible for random playback again.')) return;
        try {
            await ctx.clock('reset-podcast-history', {}, button, {
                busyText: 'Resetting play history...',
                done: 'Podcast play history reset',
                errorText: 'Podcast play history could not be reset'
            });
            await refreshSummary(); await refreshLibrary();
        } catch (_) {}
    });

    ctx.on('click', '#delete-all-podcasts', async (_, button) => {
        if (!confirm('Delete ALL podcast MP3 files?')) return;
        try {
            await ctx.update('/api/v1/assets/podcasts/delete-all', {
                method: 'POST',
                body: new URLSearchParams({format: 'json'}),
                button,
                busyText: 'Deleting all podcasts...',
                done: 'All podcasts deleted',
                errorText: 'Podcast library could not be cleared'
            });
            await refreshSummary(); await refreshLibrary();
        } catch (_) {}
    });

    ctx.on('input', '#podcast-volume', (_, slider) => {
        ctx.setText('#podcast-volume-value', `${slider.value}%`);
    });

    const refresh = async () => {
        const [statusResult] = await Promise.allSettled([
            ctx.status.refresh(),
            refreshSummary(),
            refreshLibrary(),
            refreshImport()
        ]);
        if (statusResult.status === 'fulfilled') renderStatus(statusResult.value || ctx.status.get());
    };

    ctx.status.subscribe(renderStatus);
    await refresh();
    return {refresh, unmount() { if (importTimer) clearTimeout(importTimer); }};
}
