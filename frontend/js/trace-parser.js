// Trace contract parser — mirrors src/app/HtmlTraceWriter.cpp output.
// Accepts: self-contained replay .html (const TRACE = {...};) or raw .json.
(function (NS) {
    'use strict';

    function parseTrace(text, fallbackName) {
        if (typeof text !== 'string' || text.length === 0) throw new Error('文件为空');
        const trimmed = text.slice(0, 4096);
        let json;
        if (trimmed.includes('const TRACE = ')) {
            const start = text.indexOf('const TRACE = ') + 'const TRACE = '.length;
            const endCandidates = [';\nconst canvas', ';\nconst ', ';'];
            let end = -1;
            for (const cand of endCandidates) {
                end = text.indexOf(cand, start);
                if (end >= 0) break;
            }
            if (end < 0) throw new Error('轨迹 HTML 缺少结尾标记，不是 HtmlTraceWriter 的输出');
            json = text.slice(start, end);
        } else if (trimmed.trimStart().startsWith('{')) {
            json = text;
        } else {
            throw new Error('无法识别的轨迹文件：既不是 trace HTML 也不是 JSON');
        }
        let data;
        try {
            data = JSON.parse(json);
        } catch (err) {
            throw new Error('轨迹 JSON 解析失败：' + err.message);
        }
        return validateTrace(data, fallbackName);
    }

    function validateTrace(data, fallbackName) {
        if (!data || typeof data !== 'object') throw new Error('轨迹根节点必须是对象');
        const b = data.bounds || {};
        const bounds = {
            minX: finite(b.minX, 0), minY: finite(b.minY, 0),
            maxX: finite(b.maxX, 2000), maxY: finite(b.maxY, 2000)
        };
        if (!(bounds.maxX > bounds.minX) || !(bounds.maxY > bounds.minY)) {
            throw new Error('bounds 无效');
        }
        if (!Array.isArray(data.frames)) throw new Error('缺少 frames 数组');
        const frames = data.frames.map(function (f, index) {
            if (!f || typeof f !== 'object') throw new Error('第 ' + index + ' 帧无效');
            const p = sanitizeBodies(f.p);
            const a = sanitizeBodies(f.a);
            return {
                tick: unsigned(f.tick, index),
                // Optional counts: default to sanitized body counts (matches writer semantics).
                producers: optionalCount(f.producers, p.length),
                agents: optionalCount(f.agents, a.length),
                totalEnergy: finite(f.totalEnergy, 0),
                p: p, a: a
            };
        });
        for (let i = 1; i < frames.length; ++i) {
            if (frames[i].tick < frames[i - 1].tick) {
                throw new Error('帧 tick 非递增（第 ' + i + ' 帧）');
            }
        }
        return {
            name: typeof data.name === 'string' && data.name ? data.name : (fallbackName || 'trace'),
            meta: data.meta && typeof data.meta === 'object' ? data.meta : {},
            bounds: bounds, frames: frames
        };
    }

    function sanitizeBodies(raw) {
        if (!Array.isArray(raw)) return [];
        const out = [];
        for (const body of raw) {
            if (!Array.isArray(body) || body.length < 3) continue;
            const x = Number(body[0]), y = Number(body[1]);
            if (!Number.isFinite(x) || !Number.isFinite(y)) continue;
            out.push([x, y, finite(body[2], 0), Math.max(finite(body[3], 1), 1e-9)]);
        }
        return out;
    }

    function finite(value, fallback) {
        const n = Number(value);
        return Number.isFinite(n) ? n : fallback;
    }

    function optionalCount(value, fallback) {
        if (value === undefined || value === null || value === '') return fallback;
        return unsigned(value, -1);
    }

    function unsigned(value, index) {
        const n = Number(value);
        if (!Number.isInteger(n) || n < 0) throw new Error('第 ' + index + ' 帧 tick/计数必须是非负整数');
        return n;
    }

    NS.parseTrace = parseTrace;
    NS.validateTrace = validateTrace;
})(window.EcoScope = window.EcoScope || {});
