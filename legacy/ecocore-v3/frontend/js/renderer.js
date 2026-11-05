// World canvas renderer: camera, day-night backdrop, bloom, trails. Zero deps.
(function (NS) {
    'use strict';
    const TAU = Math.PI * 2;

    function createWorldRenderer(canvas) {
        const ctx = canvas.getContext('2d');
        let trace = null;
        let width = 0, height = 0, dpr = 1;
        const cam = { scale: 1, cx: 1000, cy: 1000, fit: 1 };
        const opts = { bloom: 0.7, trails: 5, sizeScale: 1, dayNight: true };
        let camKey = '';
        let lastDrawnIndex = -1;

        function resize() {
            const rect = canvas.getBoundingClientRect();
            if (rect.width < 2 || rect.height < 2) return;
            dpr = Math.max(1, window.devicePixelRatio || 1);
            const w = Math.max(1, Math.round(rect.width * dpr));
            const h = Math.max(1, Math.round(rect.height * dpr));
            if (canvas.width !== w || canvas.height !== h) {
                canvas.width = w; canvas.height = h;
                width = w; height = h;
                if (trace) fitCamera();
                else camKey = '';
            }
        }

        function setTrace(next) {
            trace = next;
            lastDrawnIndex = -1;
            fitCamera();
        }

        function fitCamera() {
            if (!trace || width < 2) return;
            const b = trace.bounds;
            const worldW = Math.max(1e-9, b.maxX - b.minX);
            const worldH = Math.max(1e-9, b.maxY - b.minY);
            const margin = 0.94;
            cam.fit = Math.min(width / worldW, height / worldH) * margin;
            cam.scale = cam.fit;
            cam.cx = (b.minX + b.maxX) / 2;
            cam.cy = (b.minY + b.maxY) / 2;
            camKey = 'fit';
        }

        function resetView() { fitCamera(); }

        function screenToWorld(sx, sy) {
            return {
                x: (sx - width / 2) / cam.scale + cam.cx,
                y: (height / 2 - sy) / cam.scale + cam.cy
            };
        }

        function panBy(dxPx, dyPx) {
            cam.cx -= dxPx / cam.scale;
            cam.cy += dyPx / cam.scale;
            clampPan();
        }

        function zoomAt(sx, sy, factor) {
            const before = screenToWorld(sx, sy);
            const next = Math.min(cam.fit * 64, Math.max(cam.fit * 0.25, cam.scale * factor));
            if (next === cam.scale) return;
            cam.scale = next;
            const after = screenToWorld(sx, sy);
            cam.cx += before.x - after.x;
            cam.cy += before.y - after.y;
            clampPan();
        }

        function clampPan() {
            if (!trace) return;
            const b = trace.bounds;
            const worldW = Math.max(1e-9, b.maxX - b.minX);
            const worldH = Math.max(1e-9, b.maxY - b.minY);
            const halfW = (width / 2) / cam.scale;
            const halfH = (height / 2) / cam.scale;
            const padX = Math.min(worldW * 0.25, halfW * 0.9);
            const padY = Math.min(worldH * 0.25, halfH * 0.9);
            cam.cx = Math.min(b.maxX + padX, Math.max(b.minX - padX, cam.cx));
            cam.cy = Math.min(b.maxY + padY, Math.max(b.minY - padY, cam.cy));
        }

        function bodyRadius(ratio, zoom) {
            const zoomGrow = Math.pow(zoom, 0.42);
            return (1.6 + 4.6 * ratio) * opts.sizeScale * zoomGrow * dpr;
        }

        function drawFrame(frame, index) {
            if (!trace || !frame) return;
            const jumped = index !== lastDrawnIndex + 1 && index !== lastDrawnIndex;
            const key = [cam.scale, cam.cx, cam.cy, width, height, opts.trails].join('|');
            const fullClear = jumped || key !== camKey || opts.trails === 0;
            camKey = key;
            lastDrawnIndex = index;

            paintBackdrop(frame, fullClear ? 1 : 1 / (opts.trails + 2));

            const b = trace.bounds;
            const s2x = function (x) { return (x - cam.cx) * cam.scale + width / 2; };
            const s2y = function (y) { return height / 2 - (y - cam.cy) * cam.scale; };
            const zoom = cam.scale / Math.max(1e-9, cam.fit);
            const bloom = opts.bloom;
            const heavy = frame.p.length + frame.a.length > 2600;
            if (bloom > 0.02 && !heavy) {
                ctx.shadowBlur = bloom * 16 * dpr;
            } else {
                ctx.shadowBlur = 0;
            }

            drawBodies(frame.p, s2x, s2y, zoom, [72, 220, 128]);
            drawBodies(frame.a, s2x, s2y, zoom, [255, 166, 86]);
            ctx.shadowBlur = 0;
        }

        function drawBodies(bodies, s2x, s2y, zoom, rgb) {
            for (const body of bodies) {
                const ratio = Math.max(0, Math.min(1, body[3] > 0 ? body[2] / body[3] : 0));
                const r = bodyRadius(ratio, zoom);
                if (r <= 0.2) continue;
                const sx = s2x(body[0]), sy = s2y(body[1]);
                if (sx < -20 || sx > width + 20 || sy < -20 || sy > height + 20) continue;
                ctx.beginPath();
                ctx.fillStyle = 'rgba(' + rgb[0] + ',' + rgb[1] + ',' + rgb[2] + ',' + (0.32 + 0.68 * ratio).toFixed(3) + ')';
                if (ctx.shadowBlur > 0) ctx.shadowColor = 'rgba(' + rgb[0] + ',' + rgb[1] + ',' + rgb[2] + ',0.9)';
                ctx.arc(sx, sy, r, 0, TAU);
                ctx.fill();
            }
        }

        function paintBackdrop(frame, alpha) {
            let r = 6, g = 10, b = 20;
            if (opts.dayNight) {
                const d = NS.daylightAt(frame.tick);
                r = Math.round(6 + 30 * d);
                g = Math.round(10 + 40 * d);
                b = Math.round(20 + 58 * d);
            }
            if (alpha >= 1) {
                const grad = ctx.createLinearGradient(0, 0, 0, height);
                grad.addColorStop(0, 'rgb(' + Math.min(255, r + 6) + ',' + Math.min(255, g + 8) + ',' + Math.min(255, b + 14) + ')');
                grad.addColorStop(1, 'rgb(' + r + ',' + g + ',' + b + ')');
                ctx.fillStyle = grad;
            } else {
                ctx.fillStyle = 'rgba(' + r + ',' + g + ',' + b + ',' + alpha.toFixed(3) + ')';
            }
            ctx.fillRect(0, 0, width, height);

            if (opts.dayNight && alpha >= 1) {
                const d = NS.daylightAt(frame.tick);
                if (d > 0.05) {
                    const phase = NS.dayPhase(frame.tick);
                    const sunX = width * (0.5 + 0.55 * Math.cos(TAU * phase));
                    const sunY = height * (0.5 - 0.55 * Math.sin(TAU * phase));
                    const glow = ctx.createRadialGradient(sunX, sunY, 0, sunX, sunY, Math.max(width, height) * 0.55);
                    glow.addColorStop(0, 'rgba(255, 214, 140,' + (0.16 * d).toFixed(3) + ')');
                    glow.addColorStop(1, 'rgba(255, 214, 140, 0)');
                    ctx.fillStyle = glow;
                    ctx.fillRect(0, 0, width, height);
                }
            }
        }

        function setOpts(patch) { Object.assign(opts, patch); }

        return {
            resize: resize, setTrace: setTrace, drawFrame: drawFrame,
            setOpts: setOpts, resetView: resetView, panBy: panBy, zoomAt: zoomAt,
            camera: function () { return cam; }
        };
    }

    NS.createWorldRenderer = createWorldRenderer;
})(window.EcoScope = window.EcoScope || {});
