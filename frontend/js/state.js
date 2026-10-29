// Minimal observable app state.
(function (NS) {
    'use strict';
    NS.createAppState = function () {
        const listeners = new Set();
        const state = {
            trace: null, index: 0, playing: false, speed: 1, loop: true,
            lastError: ''
        };
        function get() { return state; }
        function set(patch) {
            Object.assign(state, patch);
            for (const fn of listeners) fn(state);
        }
        function on(fn) {
            listeners.add(fn);
            return function off() { listeners.delete(fn); };
        }
        return { get: get, set: set, on: on };
    };
})(window.EcoScope = window.EcoScope || {});
