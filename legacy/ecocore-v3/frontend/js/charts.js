// Sparkline charts: dim full series + bright progress up to current frame.
(function (NS) {
    'use strict';

    function createSparkline(canvas, seriesDefs) {
        const ctx = canvas.getContext('2d');
        let trace = null;
        let index = -1;

        const ro = new ResizeObserver(function () { draw(); });
        ro.observe(canvas);

        function setData(next) {
            trace = next;
            index = trace ? trace.frames.length - 1 : -1;
            draw();
        }

        function setIndex(i) {
            if (i === index) return;
            index = i;
            draw();
        }

        function draw() {
            const rect = canvas.getBoundingClientRect();
            if (rect.width < 4 || rect.height < 4) return;
            const dpr = Math.max(1, window.devicePixelRatio || 1);
            const w = Math.round(rect.width * dpr), h = Math.round(rect.height * dpr);
            if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
            ctx.clearRect(0, 0, w, h);
            if (!trace || trace.frames.length < 2) {
                ctx.fillStyle = 'rgba(143, 163, 200, .5)';
                ctx.font = (11 * dpr) + 'px system-ui';
                ctx.textAlign = 'center';
                ctx.fillText('等待轨迹…', w / 2, h / 2);
                return;
            }

            const frames = trace.frames;
            const n = frames.length;
            const padL = 2 * dpr, padR = 2 * dpr, padT = 6 * dpr, padB = 4 * dpr;
            const plotW = w - padL - padR, plotH = h - padT - padB;
            const columns = Math.max(2, Math.min(n, Math.floor(plotW / (2 * dpr))));

            // grid baseline
            ctx.strokeStyle = 'rgba(122, 152, 220, .12)';
            ctx.lineWidth = 1;
            for (let g = 1; g <= 2; ++g) {
                const gy = padT + (plotH * g) / 3;
                ctx.beginPath(); ctx.moveTo(padL, gy); ctx.lineTo(w - padR, gy); ctx.stroke();
            }

            const maxima = seriesDefs.map(function (def) {
                let max = 1e-9;
                for (const f of frames) max = Math.max(max, def.value(f));
                return max;
            });

            seriesDefs.forEach(function (def, si) {
                const max = maxima[si];
                const sampled = sample(def.value, columns);
                strokeSeries(sampled, max, def.color, 0.22, plotW, plotH, padL, padT);
                const upto = Math.max(2, Math.round(((index + 1) / n) * columns));
                strokeSeries(sampled.slice(0, upto), max, def.color, 0.95, plotW, plotH, padL, padT, true);
            });

            // current marker for first series
            if (index >= 0 && index < n) {
                const def = seriesDefs[0];
                const x = padL + (index / (n - 1)) * plotW;
                const y = padT + plotH * (1 - def.value(frames[index]) / maxima[0]);
                ctx.beginPath();
                ctx.fillStyle = def.color;
                ctx.arc(x, y, 2.4 * dpr, 0, Math.PI * 2);
                ctx.fill();
            }
        }

        function sample(getter, columns) {
            const frames = trace.frames;
            const n = frames.length;
            const out = [];
            for (let c = 0; c < columns; ++c) {
                const a = Math.floor((c * n) / columns);
                const b = Math.max(a + 1, Math.floor(((c + 1) * n) / columns));
                let peak = getter(frames[a]);
                for (let i = a + 1; i < b; ++i) peak = Math.max(peak, getter(frames[i]));
                out.push(peak);
            }
            return out;
        }

        function strokeSeries(values, max, color, alpha, plotW, plotH, padL, padT, glow) {
            if (values.length < 2) return;
            ctx.beginPath();
            for (let i = 0; i < values.length; ++i) {
                const x = padL + (i / (values.length - 1)) * plotW;
                const y = padT + plotH * (1 - Math.min(1, values[i] / max));
                if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
            }
            ctx.strokeStyle = color.replace('%A', alpha.toFixed(2));
            ctx.lineWidth = glow ? 2 : 1.25;
            ctx.lineJoin = 'round';
            if (glow) { ctx.shadowColor = color.replace('%A', '0.8'); ctx.shadowBlur = 6; }
            ctx.stroke();
            ctx.shadowBlur = 0;
        }

        return { setData: setData, setIndex: setIndex, draw: draw };
    }

    NS.createSparkline = createSparkline;
    NS.POP_SERIES = [
        { value: function (f) { return f.producers; }, color: 'rgba(72, 220, 128, %A)' },
        { value: function (f) { return f.agents; }, color: 'rgba(255, 166, 86, %A)' }
    ];
    NS.ENERGY_SERIES = [{ value: function (f) { return f.totalEnergy; }, color: 'rgba(127, 178, 255, %A)' }];
})(window.EcoScope = window.EcoScope || {});
