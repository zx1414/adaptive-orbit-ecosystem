/* agentsim 轨迹文件载入 — file input + 窗口级拖拽 · 零依赖
 * 职责：读入用户 JSON 文件 → JSON.parse → 回调消费（错误一律 toast，不白屏）。
 * 与 TraceParser 的契约校验解耦：这里只管字节→对象。
 */
window.TraceIO = (function () {
  'use strict';

  function bindHandlers(opts) {
    var fileInput = opts.fileInput;
    var loadBtn = opts.loadBtn;
    var dropCover = opts.dropCover;
    var onTrace = opts.onTrace || function () {};
    var onError = opts.onError || function () {};
    var addWin = window.addEventListener
      ? function (ev, fn, opt) { window.addEventListener(ev, fn, opt); }
      : function () {};

    function readFile(file) {
      if (!file) return;
      var rd = new FileReader();
      rd.onload = function () {
        try {
          onTrace(JSON.parse(rd.result), file.name);
        } catch (err) {
          onError('解析失败：' + err.message);
        }
      };
      rd.onerror = function () { onError('读取文件失败'); };
      rd.readAsText(file, 'utf-8');
    }

    if (loadBtn) loadBtn.addEventListener('click', function () { if (fileInput) fileInput.click(); });
    if (fileInput) fileInput.addEventListener('change', function () {
      if (fileInput.files && fileInput.files[0]) readFile(fileInput.files[0]);
      fileInput.value = '';
    });

    if (dropCover) {
      ['dragenter', 'dragover'].forEach(function (ev) {
        addWin(ev, function (e) { e.preventDefault(); dropCover.classList.add('on'); });
      });
      addWin('dragleave', function (e) {
        if (e.relatedTarget === null) dropCover.classList.remove('on');
      });
      addWin('drop', function (e) {
        e.preventDefault();
        dropCover.classList.remove('on');
        var f = e.dataTransfer && e.dataTransfer.files && e.dataTransfer.files[0];
        if (f) readFile(f);
      });
    }

    return { readFile: readFile };
  }

  return { bindHandlers: bindHandlers };
})();
