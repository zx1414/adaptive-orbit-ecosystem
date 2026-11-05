// EcoScope app wiring: transport, DnD, keyboard, HUD, cinema. Zero deps.
(function (NS) {
    'use strict';
    const $ = function (id) { return document.getElementById(id); };
    const DAY_PERIOD = NS.DAY_PERIOD;

    function boot() {
        const state = NS.createAppState();
        const canvas = $('world');
        const renderer = NS.createWorldRenderer(canvas);
        const popChart = NS.createSparkline($('chart-pop'), NS.POP_SERIES);
        const energyChart = NS.createSparkline($('chart-energy'), NS.ENERGY_SERIES);

        let playTimer = null;
        let fpsEma = 0;
        let lastRaf = 0;
        let lastFpsUi = 0;

        // ---------- loading ----------
        function loadValidated(trace) {
            state.set({ trace: trace, index: 0, playing: false });
            stopPlay();
            renderer.setTrace(trace);
            renderer.resize();
            renderer.resetView();
            popChart.setData(trace);
            energyChart.setData(trace);
            $('timeline').max = String(trace.frames.length - 1);
            $('timeline').value = '0';
            $('empty-state').style.display = 'none';
            $('trace-name').textContent = trace.name;
            $('trace-name').title = trace.name;
            applyFrame(0);
            toast('已载入「' + trace.name + '」· ' + trace.frames.length + ' 帧 · 末帧 tick ' +
                (trace.frames.length ? trace.frames[trace.frames.length - 1].tick : 0));
        }

        function loadText(text, name) {
            try {
                loadValidated(NS.parseTrace(text, name));
            } catch (err) {
                toast(String(err.message || err), true);
            }
        }

        function loadDemo() {
            const demo = window.EcoScopeDemo;
            if (!demo || !demo.data) { toast('内置示例缺失', true); return; }
            loadValidated(NS.validateTrace(Object.assign({ name: demo.name }, demo.data), demo.name));
        }

        // ---------- frame application ----------
        function applyFrame(index) {
            const trace = state.get().trace;
            if (!trace) return;
            index = Math.max(0, Math.min(trace.frames.length - 1, index));
            if (index !== state.get().index) state.set({ index: index });
            const frame = trace.frames[index];
            renderer.drawFrame(frame, index);
            popChart.setIndex(index);
            energyChart.setIndex(index);

            $('hud-tick').textContent = 'tick ' + frame.tick;
            $('hud-frame').textContent = 'frame ' + (index + 1) + ' / ' + trace.frames.length;
            const day = NS.dayLabel(frame.tick);
            $('hud-day').textContent = '第 ' + day.day + '天 ' + day.clock;
            $('time-now').textContent = 'tick ' + frame.tick;
            const last = trace.frames[trace.frames.length - 1];
            $('time-total').textContent = 'tick ' + last.tick;
            const slider = $('timeline');
            if (Number(slider.value) !== index) slider.value = String(index);

            const stats = document.querySelectorAll('[data-stat]');
            const daylightPct = Math.round(NS.daylightAt(frame.tick) * 100);
            for (const el of stats) {
                const key = el.getAttribute('data-stat');
                if (key === 'producers') el.textContent = frame.producers.toLocaleString();
                else if (key === 'agents') el.textContent = frame.agents.toLocaleString();
                else if (key === 'energy') el.textContent = formatEnergy(frame.totalEnergy);
                else if (key === 'daylight') el.textContent = daylightPct + '%';
            }
        }

        function formatEnergy(v) {
            if (!Number.isFinite(v)) return '—';
            if (Math.abs(v) >= 1e6) return (v / 1e6).toFixed(2) + 'M';
            if (Math.abs(v) >= 1e4) return (v / 1e3).toFixed(2) + 'k';
            return v.toLocaleString(undefined, { maximumFractionDigits: 1 });
        }

        // ---------- transport ----------
        function play() {
            const trace = state.get().trace;
            if (!trace || trace.frames.length < 2) return;
            if (state.get().playing) return;
            if (state.get().index >= trace.frames.length - 1) state.set({ index: 0 });
            state.set({ playing: true });
            $('btn-play').textContent = '⏸';
            lastRaf = performance.now();
            requestAnimationFrame(loop);
        }
        function stopPlay() {
            state.set({ playing: false });
            $('btn-play').textContent = '▶';
            if (playTimer) { cancelAnimationFrame(playTimer); playTimer = null; }
        }
        function loop(now) {
            if (!state.get().playing) return;
            const dt = Math.min(250, now - lastRaf);
            lastRaf = now;
            updateFps(now, dt);
            const trace = state.get().trace;
            const stepMs = 80 / state.get().speed;
            const advance = dt / stepMs;
            let next = state.get().index + advance;
            if (next >= trace.frames.length - 1) {
                if (state.get().loop) next = 0;
                else { applyFrame(Math.floor(next)); stopPlay(); return; }
            }
            applyFrame(Math.floor(next));
            playTimer = requestAnimationFrame(loop);
        }
        function updateFps(now, dt) {
            if (dt > 0) fpsEma = fpsEma === 0 ? 1000 / dt : fpsEma * 0.9 + (1000 / dt) * 0.1;
            if (now - lastFpsUi > 500) {
                lastFpsUi = now;
                $('hud-fps').textContent = Math.round(fpsEma) + ' fps';
            }
        }

        // ---------- events ----------
        $('btn-play').addEventListener('click', function () {
            state.get().playing ? stopPlay() : play();
        });
        $('btn-step-back').addEventListener('click', function () { stopPlay(); applyFrame(state.get().index - 1); });
        $('btn-step-fwd').addEventListener('click', function () { stopPlay(); applyFrame(state.get().index + 1); });
        $('timeline').addEventListener('input', function () {
            stopPlay();
            applyFrame(Number(this.value));
        });
        $('speed').addEventListener('change', function () {
            state.set({ speed: Number(this.value) || 1 });
        });
        $('opt-loop').addEventListener('change', function () { state.set({ loop: this.checked }); });

        $('btn-demo').addEventListener('click', loadDemo);
        $('btn-open').addEventListener('click', function () { $('file-input').click(); });
        $('file-input').addEventListener('change', function () {
            const file = this.files && this.files[0];
            if (!file) return;
            const reader = new FileReader();
            reader.onload = function () { loadText(String(reader.result), file.name); };
            reader.onerror = function () { toast('读取文件失败', true); };
            reader.readAsText(file);
            this.value = '';
        });

        // drag & drop
        let dragDepth = 0;
        window.addEventListener('dragenter', function (e) {
            e.preventDefault();
            ++dragDepth;
            document.body.classList.add('dragging');
        });
        window.addEventListener('dragleave', function () {
            if (--dragDepth <= 0) { dragDepth = 0; document.body.classList.remove('dragging'); }
        });
        window.addEventListener('dragover', function (e) { e.preventDefault(); });
        window.addEventListener('drop', function (e) {
            e.preventDefault();
            dragDepth = 0;
            document.body.classList.remove('dragging');
            const file = e.dataTransfer && e.dataTransfer.files && e.dataTransfer.files[0];
            if (!file) return;
            const reader = new FileReader();
            reader.onload = function () { loadText(String(reader.result), file.name); };
            reader.onerror = function () { toast('读取文件失败', true); };
            reader.readAsText(file);
        });

        // render options
        const bindRange = function (id, valueId, apply) {
            const input = $(id);
            input.addEventListener('input', function () {
                $(valueId).textContent = Number(this.value).toFixed(2).replace(/\.00$/, '');
                apply(Number(this.value));
            });
        };
        bindRange('opt-bloom', 'val-bloom', function (v) { renderer.setOpts({ bloom: v }); });
        bindRange('opt-trails', 'val-trails', function (v) {
            $('val-trails').textContent = String(v);
            renderer.setOpts({ trails: v });
        });
        bindRange('opt-scale', 'val-scale', function (v) { renderer.setOpts({ sizeScale: v }); });
        $('opt-daynight').addEventListener('change', function () {
            renderer.setOpts({ dayNight: this.checked });
        });
        $('btn-reset-view').addEventListener('click', function () { renderer.resetView(); });
        $('btn-cinema').addEventListener('click', toggleCinema);

        // canvas pan & zoom
        let dragging = false, lastX = 0, lastY = 0;
        canvas.addEventListener('pointerdown', function (e) {
            dragging = true; lastX = e.clientX; lastY = e.clientY;
            canvas.classList.add('dragging');
            canvas.setPointerCapture(e.pointerId);
        });
        canvas.addEventListener('pointermove', function (e) {
            if (!dragging) return;
            renderer.panBy(e.clientX - lastX, e.clientY - lastY);
            lastX = e.clientX; lastY = e.clientY;
            const trace = state.get().trace;
            if (trace) renderer.drawFrame(trace.frames[state.get().index], state.get().index);
        });
        const endDrag = function () { dragging = false; canvas.classList.remove('dragging'); };
        canvas.addEventListener('pointerup', endDrag);
        canvas.addEventListener('pointercancel', endDrag);
        canvas.addEventListener('wheel', function (e) {
            e.preventDefault();
            const rect = canvas.getBoundingClientRect();
            const sx = (e.clientX - rect.left) * (canvas.width / rect.width);
            const sy = (e.clientY - rect.top) * (canvas.height / rect.height);
            renderer.zoomAt(sx, sy, Math.exp(-e.deltaY * 0.0012));
            const trace = state.get().trace;
            if (trace) renderer.drawFrame(trace.frames[state.get().index], state.get().index);
        }, { passive: false });

        // keyboard
        window.addEventListener('keydown', function (e) {
            if (e.target && /input|select|textarea/i.test(e.target.tagName)) return;
            switch (e.key) {
                case ' ': e.preventDefault(); state.get().playing ? stopPlay() : play(); break;
                case 'ArrowLeft': stopPlay(); applyFrame(state.get().index - 1); break;
                case 'ArrowRight': stopPlay(); applyFrame(state.get().index + 1); break;
                case 'Home': stopPlay(); applyFrame(0); break;
                case 'End': stopPlay(); applyFrame(state.get().trace ? state.get().trace.frames.length - 1 : 0); break;
                case 'r': case 'R': renderer.resetView(); break;
                case 'f': case 'F': toggleCinema(); break;
            }
        });

        function toggleCinema() { document.body.classList.toggle('cinema'); renderer.resize(); }

        // resize
        const stageRo = new ResizeObserver(function () {
            renderer.resize();
            const trace = state.get().trace;
            if (trace) renderer.drawFrame(trace.frames[state.get().index], state.get().index);
        });
        stageRo.observe($('stage'));
        window.addEventListener('resize', function () { renderer.resize(); });

        // auto-load demo; #autoplay starts the transport (also used by headless acceptance)
        if (window.EcoScopeDemo && window.EcoScopeDemo.data) {
            loadDemo();
            if (location.hash === '#autoplay') play();
        } else {
            $('hud-tick').textContent = 'tick —';
        }

        toast('提示：拖入 simulator --html 输出的轨迹文件即可回放');
    }

    function toast(msg, isErr) {
        const el = document.getElementById('toast');
        el.textContent = msg;
        el.classList.toggle('err', !!isErr);
        el.classList.add('show');
        clearTimeout(el._timer);
        el._timer = setTimeout(function () { el.classList.remove('show'); }, 3600);
    }

    NS.boot = boot;
    NS.toast = toast;
    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', boot);
    else boot();
})(window.EcoScope = window.EcoScope || {});
