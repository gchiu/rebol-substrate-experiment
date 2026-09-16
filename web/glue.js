/* web/glue.js -- the JavaScript half of the browser boundary.
 *
 * JavaScript responsibilities only:
 *   - capture the host's standard output (the WEB_SET_INT transport);
 *   - decode handle/value and update the mapped DOM element;
 *   - forward button clicks to the exported WASM entry `r0_click`.
 *
 * No counter state or application logic lives here.
 */

/* handle -> DOM element id (numeric DOM handle 1 = the counter display) */
var R0_DOM_HANDLES = { 1: 'counter' };

Module.print = function (line) {
    var s = String(line).replace(/^\s+|\s+$/g, '');
    if (s === '') return;
    var n = parseInt(s, 10);
    if (!isNaN(n) && n >= 1000000) {
        var handle = Math.floor(n / 1000000);
        var value = n % 1000000;
        var elId = R0_DOM_HANDLES[handle];
        if (elId) {
            var el = document.getElementById(elId);
            if (el) el.textContent = String(value);
        }
    } else {
        console.log('[R0]', s);   /* plain R0 output (e.g. the ready line) */
    }
};
Module.printErr = function (line) {
    console.log('[R0:err]', line);
};

Module.onRuntimeInitialized = function () {
    var btn = document.getElementById('increment');
    btn.addEventListener('click', function () {
        Module._r0_click();
    });
};
