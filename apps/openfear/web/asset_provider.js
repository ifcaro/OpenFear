// OpenFear — browser asset provider glue (Emscripten --pre-js). Backs
// EmscriptenAssetProvider (src/assets/EmscriptenAssetProvider.cpp): lets the
// user pick their own assets folder via the File System Access API (or
// <input webkitdirectory> as fallback), reading data locally and on
// demand - nothing ever gets uploaded anywhere.
//
// Deliberately dumb: raw picked-folder access only. CD-layout
// reclassification lives in C++ (openfear::assets::CdLayout), not here.
//
// Path convention: keys in ofwFiles are relative, uppercased,
// forward-slash-separated (e.g. "AV2/INTRO.AVI"), matching the native
// backend's own normalization.

Module['ofwFiles'] = new Map();

Module['ofwNormalize'] = function(p) {
    return p.replace(/\\/g, '/').replace(/^\/+/, '').toUpperCase();
};

async function ofwWalkDirectoryHandle(handle, prefix) {
    for await (const [name, child] of handle.entries()) {
        const rel = prefix ? prefix + '/' + name : name;
        if (child.kind === 'file') {
            const file = await child.getFile();
            Module['ofwFiles'].set(Module['ofwNormalize'](rel), file);
        } else {
            await ofwWalkDirectoryHandle(child, rel);
        }
    }
}

async function ofwPickFolder() {
    Module['ofwFiles'].clear();
    if (window.showDirectoryPicker) {
        try {
            const dirHandle = await window.showDirectoryPicker();
            await ofwWalkDirectoryHandle(dirHandle, '');
        } catch (err) {
            // User cancelled the picker (AbortError) - 0 files, same as an
            // empty folder, instead of an unhandled rejection that leaves
            // the caller waiting forever.
            return 0;
        }
    } else {
        await new Promise((resolve) => {
            const input = document.createElement('input');
            input.type = 'file';
            input.webkitdirectory = true;
            let resolved = false;
            const finish = () => {
                if (resolved) return;
                resolved = true;
                resolve();
            };
            input.onchange = () => {
                for (const file of input.files) {
                    // webkitRelativePath is "<picked-folder-name>/sub/path" - drop the
                    // synthetic root so keys match showDirectoryPicker's own convention.
                    const parts = file.webkitRelativePath.split('/');
                    const rel = parts.slice(1).join('/');
                    Module['ofwFiles'].set(Module['ofwNormalize'](rel), file);
                }
                finish();
            };
            // Modern browsers fire 'cancel' directly - exact, no guessing.
            input.addEventListener('cancel', finish);
            // Fallback for browsers without 'cancel': the window regains
            // focus right after the dialog closes either way. Grace period
            // is generous - a large picked folder (thousands of files, as
            // real CD dumps are) can take seconds for the browser to
            // enumerate into `input.files` before 'change' fires; firing
            // too early here would wrongly treat that as a cancel (see
            // 2026-10-08 session - confirmed live, not hypothetical).
            window.addEventListener(
                'focus',
                function onFocus() {
                    window.removeEventListener('focus', onFocus);
                    setTimeout(finish, 5000);
                },
                {once: true});
            input.click();
        });
    }
    return Module['ofwFiles'].size;
}

// "Click to start" gate: picking requires a user gesture (also satisfies
// the audio-autoplay-gesture requirement for the same click).
Module['ofwStartGate'] = function() {
    return new Promise((resolve) => {
        const div = document.createElement('div');
        div.id = 'openfear-start-gate';
        div.style.cssText =
            'position:fixed;inset:0;background:rgba(0,0,0,0.88);color:#fff;' +
            'display:flex;flex-direction:column;align-items:center;justify-content:center;' +
            'font:20px sans-serif;z-index:99999;text-align:center;padding:24px;gap:16px;';
        div.innerHTML =
            '<div>Select your Atmosfear assets folder</div>' +
            '<div style="font-size:14px;opacity:0.7;max-width:480px;">' +
            'The installed folder, or the original CD contents</div>' +
            '<button id="ofw-pick-folder" style="font:16px sans-serif;padding:10px 20px;' +
            'cursor:pointer;">Select folder</button>';
        document.body.appendChild(div);
        document.querySelector('#ofw-pick-folder').addEventListener('click', async () => {
            const count = await ofwPickFolder();
            div.remove();
            resolve(count);
        });
    });
};

Module['ofwExists'] = function(path) {
    return Module['ofwFiles'].has(Module['ofwNormalize'](path)) ? 1 : 0;
};

Module['ofwGetSize'] = function(path) {
    const f = Module['ofwFiles'].get(Module['ofwNormalize'](path));
    return f ? f.size : -1;
};

// Reads [offset, offset+length) from `path` into WASM memory at `destPtr`. Returns the number
// of bytes actually read (may be less than `length` at EOF), or 0 if the file isn't known.
Module['ofwReadSlice'] = async function(path, offset, length, destPtr) {
    const f = Module['ofwFiles'].get(Module['ofwNormalize'](path));
    if (!f) return 0;
    const end = Math.min(offset + length, f.size);
    if (end <= offset) return 0;
    const buf = await f.slice(offset, end).arrayBuffer();
    const bytes = new Uint8Array(buf);
    HEAPU8.set(bytes, destPtr);
    return bytes.length;
};

// All picked-file paths (already normalized), newline-joined - lets the C++ side build
// the CdPathRemap once, at StartGate() time.
Module['ofwListPaths'] = function() {
    return Array.from(Module['ofwFiles'].keys()).join('\n');
};
