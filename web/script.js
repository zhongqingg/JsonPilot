let jsonData = null;
let currentFilePath = "";
let modified = false;
let isPlainTextMode = false;
let plainTextContent = null;
let confirmCallback = null;
let diffData = { modified: {}, added: {}, deleted: {} };

const undoStack = [];
const redoStack = [];
const maxUndoDepth = 50;
let undoGrouping = false;
let undoGroupTimer = null;

let lastActivePath = null;
let searchActive = false;
let searchMatches = [];
let searchCurrentIdx = -1;
let searchQuery = '';
let replaceScope = null;

function pushSnapshot() {
    undoStack.push({
        data: JSON.parse(JSON.stringify(jsonData)),
        diff: JSON.parse(JSON.stringify(diffData)),
        modified: modified
    });
    if (undoStack.length > maxUndoDepth) undoStack.shift();
    redoStack.length = 0;
}

function undo() {
    if (undoStack.length === 0) return;
    redoStack.push({
        data: JSON.parse(JSON.stringify(jsonData)),
        diff: JSON.parse(JSON.stringify(diffData)),
        modified: modified
    });
    const snap = undoStack.pop();
    jsonData = snap.data;
    diffData = snap.diff;
    modified = snap.modified;
    if (undoGroupTimer) { clearTimeout(undoGroupTimer); undoGroupTimer = null; }
    undoGrouping = false;
    refreshAfterUndoRedo();
}

function redo() {
    if (redoStack.length === 0) return;
    undoStack.push({
        data: JSON.parse(JSON.stringify(jsonData)),
        diff: JSON.parse(JSON.stringify(diffData)),
        modified: modified
    });
    const snap = redoStack.pop();
    jsonData = snap.data;
    diffData = snap.diff;
    modified = snap.modified;
    if (undoGroupTimer) { clearTimeout(undoGroupTimer); undoGroupTimer = null; }
    undoGrouping = false;
    refreshAfterUndoRedo();
}

function refreshAfterUndoRedo() {
    document.getElementById("file-path-display").textContent = currentFilePath
        ? currentFilePath + (modified ? " (modified)" : "")
        : "No file selected";
    document.getElementById("btnSave").disabled = !modified || !currentFilePath;
    rerenderJson();
    expandAll();
}

function recordChange(type, path, details) {
    const key = JSON.stringify(path);
    if (type === 'modified') diffData.modified[key] = details;
    else if (type === 'added') diffData.added[key] = details;
    else if (type === 'deleted') diffData.deleted[key] = details;
}

function clearDiff() {
    diffData = { modified: {}, added: {}, deleted: {} };
}

function applyDiffMarkers() {
    document.querySelectorAll('.deleted-line, .deleted-tooltip').forEach(el => el.remove());
    document.querySelectorAll('.line-modified, .line-added, .value-modified, .key-modified').forEach(el => {
        el.classList.remove('line-modified', 'line-added', 'value-modified', 'key-modified');
    });

    function findNodeByPath(pathStr) {
        const nodes = document.querySelectorAll('.json-node');
        for (const n of nodes) { if (n.dataset.path === pathStr) return n; }
        return null;
    }
    function highlightNode(node, cls) {
        if (!node) return;
        node.querySelectorAll('.json-line').forEach(line => line.classList.add(cls));
        node.querySelectorAll('.json-value').forEach(v => v.classList.add('value-modified'));
        if (cls === 'line-modified') {
            const firstLine = node.querySelector(':scope > .json-line');
            if (firstLine) {
                firstLine.querySelectorAll('.json-key').forEach(k => k.classList.add('key-modified'));
            }
        }
    }

    for (const pathStr of Object.keys(diffData.modified)) {
        highlightNode(findNodeByPath(pathStr), 'line-modified');
    }

    for (const pathStr of Object.keys(diffData.added)) {
        highlightNode(findNodeByPath(pathStr), 'line-added');
    }

    for (const [pathStr, info] of Object.entries(diffData.deleted)) {
        const parentPathStr = JSON.stringify(info.parentPath);
        const parentNode = findNodeByPath(parentPathStr);
        if (!parentNode) continue;
        const children = parentNode.querySelector(':scope > .json-children');
        if (!children) continue;
        const index = info.position;
        const marker = document.createElement('div');
        marker.className = 'deleted-line';
        marker.dataset.path = pathStr;
        const xBtn = document.createElement('span');
        xBtn.className = 'deleted-x';
        xBtn.textContent = '✕';
        let delLabel;
        if (info.isArrayElement) {
            delLabel = '[' + info.key + '] ' + JSON.stringify(info.value, null, 2);
        } else if (info.key !== undefined && info.key !== null) {
            delLabel = '"' + info.key + '": ' + JSON.stringify(info.value, null, 2);
        } else {
            delLabel = JSON.stringify(info.value, null, 2);
        }
        xBtn.addEventListener('mouseenter', (e) => showDeletedTooltip(e, delLabel));
        xBtn.addEventListener('mouseleave', hideDeletedTooltip);
        marker.appendChild(xBtn);
        const childNodes = [...children.children];
        let insertBefore = null;
        let count = 0;
        for (const child of childNodes) {
            if (child.classList.contains('deleted-line')) continue;
            if (count === index) { insertBefore = child; break; }
            count++;
        }
        if (insertBefore) children.insertBefore(marker, insertBefore);
        else children.appendChild(marker);
    }
}

function showDeletedTooltip(e, valStr) {
    const existing = document.querySelector('.deleted-tooltip');
    if (existing) existing.remove();
    const tip = document.createElement('div');
    tip.className = 'deleted-tooltip';
    tip.textContent = valStr;
    tip.style.left = Math.min(e.clientX + 10, window.innerWidth - 460) + 'px';
    tip.style.top = Math.min(e.clientY + 10, window.innerHeight - 260) + 'px';
    document.body.appendChild(tip);
}

function hideDeletedTooltip() {
    document.querySelectorAll('.deleted-tooltip').forEach(el => el.remove());
}

/* ── Search/Replace ── */

function pathsEqual(a, b) {
    return Array.isArray(a) && Array.isArray(b)
        && a.length === b.length
        && a.every((part, i) => part === b[i]);
}

function pathStartsWith(path, prefix) {
    return Array.isArray(path) && Array.isArray(prefix)
        && path.length >= prefix.length
        && prefix.every((part, i) => path[i] === part);
}

function findNodeByPath(pathStr) {
    const nodes = document.querySelectorAll('.json-node');
    for (const node of nodes) {
        if (node.dataset.path === pathStr) return node;
    }
    return null;
}

function clearSearchDecorations() {
    document.querySelectorAll('.json-key[data-path], .json-value[data-path]').forEach(el => {
        restoreSearchTarget(el);
    });
}

function clearSearchHighlights() {
    searchActive = false;
    searchMatches = [];
    searchCurrentIdx = -1;
    searchQuery = '';
    clearSearchDecorations();
    document.getElementById('search-count').textContent = '';
    document.getElementById('replace-count').textContent = '';
    document.getElementById('replace-find-input').value = '';
    document.getElementById('replace-with-input').value = '';
    document.getElementById('replace-with-input').disabled = true;
    document.getElementById('btnReplaceConfirm').disabled = true;
    replaceMatches = [];
}

function appendTextWithMatches(parent, text, query, matchClass, replacement) {
    if (!query) {
        parent.appendChild(document.createTextNode(text));
        return 0;
    }
    const lowerText = text.toLowerCase();
    const lowerQuery = query.toLowerCase();
    let cursor = 0;
    let count = 0;
    while (cursor < text.length) {
        const idx = lowerText.indexOf(lowerQuery, cursor);
        if (idx < 0) break;
        if (idx > cursor) {
            parent.appendChild(document.createTextNode(text.slice(cursor, idx)));
        }
        const mark = document.createElement("span");
        mark.className = matchClass;
        mark.textContent = replacement !== undefined ? replacement : text.slice(idx, idx + query.length);
        parent.appendChild(mark);
        cursor = idx + query.length;
        count++;
    }
    if (cursor < text.length) {
        parent.appendChild(document.createTextNode(text.slice(cursor)));
    }
    return count;
}

function getTargetElement(match) {
    const node = findNodeByPath(JSON.stringify(match.path));
    if (!node) return null;
    const line = node.querySelector(':scope > .json-line');
    if (!line) return null;
    return match.isKey
        ? line.querySelector(':scope > .json-key[data-path]')
        : line.querySelector(':scope > .json-value[data-path]');
}

function restoreSearchTarget(el) {
    if (!el || !el.dataset.rawText) return;
    const text = el.dataset.rawText;
    const isKey = el.dataset.targetType === "key";
    const isString = el.dataset.valueType === "string";
    el.innerHTML = "";
    if (isKey) {
        appendQuote(el);
        el.appendChild(document.createTextNode(text));
        appendQuote(el);
        appendColon(el);
    } else if (isString) {
        appendQuote(el);
        el.appendChild(document.createTextNode(text));
        appendQuote(el);
    } else {
        el.appendChild(document.createTextNode(text));
    }
}

function appendQuote(parent) {
    const quote = document.createElement("span");
    quote.style.color = "#888";
    quote.textContent = '"';
    parent.appendChild(quote);
}

function appendColon(parent) {
    const colon = document.createElement("span");
    colon.className = "json-colon";
    colon.textContent = ": ";
    parent.appendChild(colon);
}

function renderDecoratedTarget(match, query, cls, replacement) {
    const el = getTargetElement(match);
    if (!el || !el.dataset.rawText) return false;
    const text = el.dataset.rawText;
    const isKey = el.dataset.targetType === "key";
    const isString = el.dataset.valueType === "string";
    el.innerHTML = "";
    if (isKey) appendQuote(el);
    if (isString) appendQuote(el);
    const count = appendTextWithMatches(el, text, query, cls, replacement);
    if (isKey) {
        appendQuote(el);
        appendColon(el);
    }
    if (isString) appendQuote(el);
    return count > 0;
}

function gatherSearchTargets(data, path, scopeRoot) {
    const results = [];
    if (data === null || data === undefined) return results;
    if (typeof data !== 'object') {
        const repr = typeof data === 'string' ? data : String(data);
        results.push({ path, text: repr, isKey: false });
    } else {
        const keys = Array.isArray(data) ? data.map((_, i) => i) : Object.keys(data);
        const isArray = Array.isArray(data);
        for (const key of keys) {
            const val = data[key];
            const childPath = [...path, key];
            if (!isArray) {
                results.push({ path: childPath, text: String(key), isKey: true });
            }
            if (val !== null && typeof val === 'object') {
                results.push(...gatherSearchTargets(val, childPath, scopeRoot));
            } else if (typeof val === 'string') {
                results.push({ path: childPath, text: val, isKey: false });
            } else {
                const repr = val === null ? 'null' : String(val);
                results.push({ path: childPath, text: repr, isKey: false });
            }
        }
    }
    return results;
}

function highlightMatches() {
    clearSearchDecorations();
    for (let i = 0; i < searchMatches.length; i++) {
        const m = searchMatches[i];
        const cls = (i === searchCurrentIdx) ? 'search-match-active' : 'search-match';
        renderDecoratedTarget(m, searchQuery, cls);
    }
    if (searchCurrentIdx >= 0 && searchCurrentIdx < searchMatches.length) {
        const m = searchMatches[searchCurrentIdx];
        const node = findNodeByPath(JSON.stringify(m.path));
        if (node) node.scrollIntoView({ block: 'center' });
    }
    const cnt = document.getElementById('search-count');
    if (searchMatches.length > 0) {
        cnt.textContent = (searchCurrentIdx + 1) + '/' + searchMatches.length;
    } else {
        cnt.textContent = '0/0';
    }
}

function performSearch(query) {
    clearSearchHighlights();
    if (!query || !jsonData) {
        document.getElementById('search-count').textContent = '';
        return;
    }
    searchActive = true;
    searchQuery = query;
    const q = query.toLowerCase();
    const all = gatherSearchTargets(jsonData, [], null);
    const matches = [];
    for (const target of all) {
        if (target.text.toLowerCase().includes(q)) {
            matches.push(target);
        }
    }
    if (matches.length === 0) {
        document.getElementById('search-count').textContent = '0/0';
        return;
    }
    searchMatches = matches;
    if (lastActivePath) {
        const lastStr = JSON.stringify(lastActivePath);
        let best = 0;
        for (let i = 0; i < matches.length; i++) {
            const mStr = JSON.stringify(matches[i].path);
            if (mStr >= lastStr) { best = i; break; }
            best = i;
        }
        searchCurrentIdx = best;
    } else {
        searchCurrentIdx = 0;
    }
    highlightMatches();
}

function nextMatch() {
    if (searchMatches.length === 0) return;
    searchCurrentIdx = (searchCurrentIdx + 1) % searchMatches.length;
    highlightMatches();
}

function prevMatch() {
    if (searchMatches.length === 0) return;
    searchCurrentIdx = (searchCurrentIdx - 1 + searchMatches.length) % searchMatches.length;
    highlightMatches();
}

function toggleSearch() {
    const bar = document.getElementById('search-bar');
    if (bar.classList.contains('hidden')) {
        bar.classList.remove('hidden');
        document.getElementById('search-input').focus();
    } else {
        bar.classList.add('hidden');
        document.getElementById('replace-bar').classList.add('hidden');
        clearSearchHighlights();
    }
}

/* ── Replace ── */

function showReplaceBar(scope) {
    replaceScope = scope;
    document.getElementById('replace-bar').classList.remove('hidden');
    document.getElementById('search-bar').classList.add('hidden');
    clearSearchHighlights();
    document.getElementById('replace-find-input').value = '';
    document.getElementById('replace-with-input').value = '';
    document.getElementById('replace-with-input').disabled = true;
    document.getElementById('replace-with-input').dataset.touched = "false";
    document.getElementById('btnReplaceConfirm').disabled = true;
    document.getElementById('replace-count').textContent = '';
    document.getElementById('replace-find-input').focus();
}

let replaceMatches = [];

function isInScope(path, scope) {
    if (!scope) return true;
    if (scope.type === 'root') return true;
    if (scope.type === 'value') {
        return pathsEqual(path, scope.path);
    }
    if (scope.type === 'node') {
        return pathStartsWith(path, scope.path);
    }
    return true;
}

function performReplaceFind() {
    const findInput = document.getElementById('replace-find-input');
    const withInput = document.getElementById('replace-with-input');
    const countEl = document.getElementById('replace-count');
    const query = findInput.value;
    clearSearchDecorations();
    if (!query) {
        withInput.disabled = true;
        document.getElementById('btnReplaceConfirm').disabled = true;
        countEl.textContent = '';
        replaceMatches = [];
        return;
    }
    if (!jsonData) return;
    const q = query.toLowerCase();
    const all = gatherSearchTargets(jsonData, [], null);
    const matches = [];
    for (const target of all) {
        if (isInScope(target.path, replaceScope)) {
            if (target.text.toLowerCase().includes(q)) {
                const cur = getValueByPath(jsonData, target.path);
                if (target.isKey || typeof cur === 'string') matches.push(target);
            }
        }
    }
    replaceMatches = matches;
    for (const m of matches) {
        renderDecoratedTarget(m, query, 'search-match');
    }
    if (matches.length > 0) {
        withInput.disabled = false;
        countEl.textContent = matches.length + ' match' + (matches.length > 1 ? 'es' : '');
        if (withInput.dataset.touched === "true") {
            updateReplacePreview();
        } else {
            document.getElementById('btnReplaceConfirm').disabled = true;
        }
    } else {
        withInput.disabled = true;
        document.getElementById('btnReplaceConfirm').disabled = true;
        countEl.textContent = '0 matches';
    }
}

function updateReplacePreview() {
    const findText = document.getElementById('replace-find-input').value;
    const replaceInput = document.getElementById('replace-with-input');
    const replaceText = replaceInput.value;
    replaceInput.dataset.touched = "true";
    clearSearchDecorations();
    if (!findText || replaceMatches.length === 0) {
        document.getElementById('btnReplaceConfirm').disabled = true;
        return;
    }
    if (replaceText === "") {
        for (const m of replaceMatches) {
            renderDecoratedTarget(m, findText, 'search-match');
        }
        document.getElementById('btnReplaceConfirm').disabled = true;
        return;
    }
    let changed = 0;
    for (const m of replaceMatches) {
        const currentText = m.isKey ? String(m.path[m.path.length - 1]) : getValueByPath(jsonData, m.path);
        if (typeof currentText !== 'string') continue;
        const newText = currentText.split(findText).join(replaceText);
        if (newText !== currentText) {
            renderDecoratedTarget(m, findText, 'replace-preview', replaceText);
            changed++;
        } else {
            renderDecoratedTarget(m, findText, 'search-match');
        }
    }
    document.getElementById('btnReplaceConfirm').disabled = (changed === 0);
}

function executeReplace() {
    const findText = document.getElementById('replace-find-input').value;
    const replaceText = document.getElementById('replace-with-input').value;
    if (!findText || replaceMatches.length === 0) return;
    const changes = [];
    for (const m of replaceMatches) {
        if (m.isKey) {
            const oldKey = String(m.path[m.path.length - 1]);
            const newKey = oldKey.split(findText).join(replaceText);
            if (newKey !== oldKey) {
                changes.push({ type: 'key', path: m.path, oldKey, newKey });
            }
            continue;
        }
        const oldVal = getValueByPath(jsonData, m.path);
        if (typeof oldVal !== 'string') continue;
        const newVal = oldVal.split(findText).join(replaceText);
        if (newVal !== oldVal) {
            changes.push({ type: 'value', path: m.path, oldVal, newVal });
        }
    }
    if (changes.length === 0) return;
    pushSnapshot();
    for (const change of changes) {
        if (change.type !== 'value') continue;
        setValueByPath(jsonData, change.path, change.newVal);
        recordChange('modified', change.path, { oldVal: change.oldVal, newVal: change.newVal });
    }
    for (const change of changes) {
        if (change.type !== 'key') continue;
        const parent = getValueByPath(jsonData, change.path.slice(0, -1));
        if (!parent || typeof parent !== 'object' || Array.isArray(parent)) continue;
        if (Object.prototype.hasOwnProperty.call(parent, change.newKey)) continue;
        const oldVal = parent[change.oldKey];
        parent[change.newKey] = oldVal;
        delete parent[change.oldKey];
        const newPath = [...change.path.slice(0, -1), change.newKey];
        recordChange('modified', newPath, { oldVal, newVal: oldVal, oldKey: change.oldKey });
    }
    markModified();
    document.getElementById('replace-bar').classList.add('hidden');
    clearSearchHighlights();
    const state = getExpandedState();
    rerenderJson();
    restoreExpandedState(state);
}

async function init() {
    await applyConfigTheme();
    await populateRootPath();
    await new Promise(r => setTimeout(r, 200));
    await loadFileTree();

    // File tree empty-space right-click: show menu
    document.getElementById("file-tree").addEventListener("contextmenu", (e) => {
        if (e.target !== document.getElementById("file-tree")) return;
        e.preventDefault();
        showFileTreeEmptyMenu(e.clientX, e.clientY);
    });

    document.getElementById("btnRefresh").addEventListener("click", loadFileTree);
    document.getElementById("btnTheme").addEventListener("click", toggleTheme);
    document.getElementById("btnExpandAll").addEventListener("click", expandAll);
    document.getElementById("btnCollapseAll").addEventListener("click", collapseAll);
    document.getElementById("btnSave").addEventListener("click", saveFile);
    document.getElementById("btnSaveAs").addEventListener("click", showSaveAsDialog);

    document.getElementById("confirm-ok").addEventListener("click", () => {
        const cb = confirmCallback;
        hideConfirm();
        if (cb) cb();
    });
    document.getElementById("confirm-cancel").addEventListener("click", hideConfirm);

    document.getElementById("save-as-ok").addEventListener("click", doSaveAs);
    document.getElementById("save-as-cancel").addEventListener("click", hideSaveAsDialog);
    document.getElementById("btnBrowse").addEventListener("click", onBrowseSaveAs);
    document.getElementById("save-as-path").addEventListener("keydown", (e) => {
        if (e.key === "Enter") doSaveAs();
    });

    document.getElementById("add-ok").addEventListener("click", doAddChild);
    document.getElementById("add-cancel").addEventListener("click", hideAddChildDialog);
    document.getElementById("add-key-input").addEventListener("keydown", (e) => {
        if (e.key === "Enter") {
            e.preventDefault();
            document.getElementById("add-value-input").focus();
        }
    });
    document.getElementById("add-value-input").addEventListener("keydown", (e) => {
        if ((e.ctrlKey || e.metaKey) && e.key === "Enter") {
            e.preventDefault();
            doAddChild();
        }
    });

    document.addEventListener("click", () => { hideContextMenu(); hideAllMenus(); });
    document.addEventListener("contextmenu", (e) => e.preventDefault());
    document.getElementById("btnUndo").addEventListener("click", undo);
    document.getElementById("btnRedo").addEventListener("click", redo);
    document.getElementById("btnSearch").addEventListener("click", toggleSearch);
    setupDragAndDrop();

    // Path bar
    document.getElementById("btnBrowseFolder").addEventListener("click", onBrowseRootFolder);
    document.getElementById("path-input").addEventListener("keydown", (e) => {
        if (e.key === "Enter") applyRootPath();
    });
    document.getElementById("path-input").addEventListener("blur", () => {
        applyRootPath();
    });

    document.getElementById("search-input").addEventListener("input", (e) => {
        performSearch(e.target.value);
    });
    document.getElementById("search-input").addEventListener("keyup", (e) => {
        performSearch(e.target.value);
    });
    document.getElementById("search-input").addEventListener("keydown", (e) => {
        if (e.key === "Enter") {
            if (e.shiftKey) prevMatch();
            else nextMatch();
        }
        if (e.key === "Backspace" && !e.target.value) {
            toggleSearch();
        }
    });
    document.getElementById("search-prev").addEventListener("click", prevMatch);
    document.getElementById("search-next").addEventListener("click", nextMatch);
    document.getElementById("btnSearchClear").addEventListener("click", () => {
        document.getElementById('search-input').value = '';
        document.getElementById('search-bar').classList.add('hidden');
        clearSearchHighlights();
    });

    document.getElementById("replace-find-input").addEventListener("input", performReplaceFind);
    document.getElementById("replace-find-input").addEventListener("keyup", performReplaceFind);
    document.getElementById("replace-with-input").addEventListener("input", updateReplacePreview);
    document.getElementById("replace-with-input").addEventListener("keyup", updateReplacePreview);
    document.getElementById("btnReplaceConfirm").addEventListener("click", executeReplace);
    document.getElementById("btnReplaceCancel").addEventListener("click", () => {
        document.getElementById('replace-bar').classList.add('hidden');
        clearSearchHighlights();
    });

    document.addEventListener("keydown", (e) => {
        if (e.key === "Escape") {
            if (!document.getElementById("replace-bar").classList.contains("hidden")) {
                document.getElementById('replace-bar').classList.add('hidden');
                clearSearchHighlights();
                return;
            }
            if (!document.getElementById("search-bar").classList.contains("hidden")) {
                document.getElementById('search-bar').classList.add('hidden');
                clearSearchHighlights();
                return;
            }
            if (!document.getElementById("add-dialog").classList.contains("hidden")) {
                hideAddChildDialog();
                return;
            }
            if (!document.getElementById("context-menu").classList.contains("hidden")) {
                hideContextMenu();
            }
        }
        if ((e.ctrlKey || e.metaKey) && e.key === "z") {
            e.preventDefault();
            undo();
        }
        if ((e.ctrlKey || e.metaKey) && e.key === "y") {
            e.preventDefault();
            redo();
        }
        if ((e.ctrlKey || e.metaKey) && e.key === "f") {
            e.preventDefault();
            toggleSearch();
        }
        if ((e.ctrlKey || e.metaKey) && e.key === "h") {
            e.preventDefault();
            showReplaceBar({ type: 'root' });
        }
        if (searchActive && !e.ctrlKey && !e.metaKey) {
            const active = document.activeElement;
            const isTyping = active && (
                active.tagName === "INPUT" ||
                active.tagName === "TEXTAREA" ||
                active.isContentEditable
            );
            if (isTyping) return;
            if (e.key === "n") {
                e.preventDefault();
                nextMatch();
            }
            if (e.key === "N") {
                e.preventDefault();
                prevMatch();
            }
            if (e.key === "Backspace") {
                e.preventDefault();
                document.getElementById('search-input').value = '';
                clearSearchHighlights();
            }
        }
    });

    window.onbeforeunload = (e) => {
        if (modified) {
            e.preventDefault();
            e.returnValue = "";
            return "";
        }
    };
}

async function applyConfigTheme() {
    try {
        const configStr = await get_config();
        const config = JSON.parse(configStr);
        const theme = config.theme || "dark";
        document.documentElement.dataset.theme = theme;
        updateThemeButton(theme);
    } catch (e) {
        document.documentElement.dataset.theme = "dark";
        updateThemeButton("dark");
    }
}

function toggleTheme() {
    const current = document.documentElement.dataset.theme;
    const next = current === "dark" ? "light" : "dark";
    document.documentElement.dataset.theme = next;
    updateThemeButton(next);
}

function updateThemeButton(theme) {
    document.getElementById("btnTheme").textContent = theme === "dark" ? "☀️" : "🌙";
}

let lastRootPath = "";

async function populateRootPath() {
    try {
        const configStr = await get_config();
        const config = JSON.parse(configStr);
        lastRootPath = config.root_dir || "";
        document.getElementById("path-input").value = lastRootPath;
    } catch (e) {
        console.error("Failed to get config:", e);
    }
}

async function onBrowseRootFolder() {
    try {
        const itemPath = await browse_folder();
        if (itemPath && itemPath !== lastRootPath) {
            document.getElementById("path-input").value = itemPath;
            await applyRootPath();
        }
    } catch (e) {
        console.error("Browse folder error:", e);
    }
}

async function applyRootPath() {
    const newPath = document.getElementById("path-input").value.trim();
    if (!newPath || newPath === lastRootPath) return;
    try {
        const resultStr = await set_root_dir(newPath);
        const result = JSON.parse(resultStr);
        if (result.success) {
            lastRootPath = result.root_dir;
            document.getElementById("path-input").value = lastRootPath;
            await reloadFullTree();
        }
    } catch (e) {
        console.error("Set root dir error:", e);
    }
}

async function loadFileTree(retryCount) {
    if (retryCount === undefined) retryCount = 0;
    try {
        const text = await get_file_tree();
        const paths = text.trim().split('\n').filter(p => p);
        // Build tree from flat path list on the JS side
        const tree = {};
        for (let relPath of paths) {
            const isDir = relPath.endsWith('/');
            if (isDir) relPath = relPath.slice(0, -1);
            const parts = relPath.split('/');
            let node = tree;
            for (let i = 0; i < parts.length; i++) {
                const key = parts[i];
                if (i === parts.length - 1) {
                    if (!isDir) {
                        node[key] = { file: relPath };
                    } else if (!node[key]) {
                        node[key] = {};
                    }
                } else {
                    if (!node[key]) node[key] = {};
                    if (node[key].file) {
                        node[key] = { __files__: node[key] };
                    }
                    node = node[key];
                }
            }
        }
        renderFileTree(tree, document.getElementById("file-tree"), "", null);
        if (retryCount > 0) restoreExpandState();
        if (retryCount < 3 && Object.keys(tree).length === 0) {
            setTimeout(() => loadFileTree(retryCount + 1), 300);
        }
    } catch (err) {
        if (retryCount < 3) {
            setTimeout(() => loadFileTree(retryCount + 1), 500);
        }
    }
}

function renderFileTree(node, container, basePath, parentItem, clearContainer = true) {
    if (clearContainer) container.innerHTML = "";
    if (typeof node !== "object" || node === null) return;
    const entries = [];
    for (const key of Object.keys(node)) {
        if (key === "__files__") continue;
        entries.push([key, node[key]]);
    }
    if (node.__files__ && typeof node.__files__ === "object") {
        for (const key of Object.keys(node.__files__)) {
            entries.push([key, node.__files__[key]]);
        }
    }
    entries.sort(([a], [b]) => a.localeCompare(b));

    for (const [key, val] of entries) {
        const item = document.createElement("div");
        item.className = "file-tree-item";
        const icon = document.createElement("span");
        icon.className = "icon";
        const label = document.createElement("span");
        label.className = "label";

        const itemPath = basePath ? basePath + "/" + key : key;

        if (val && typeof val === "object" && val.file) {
            icon.textContent = "\uD83D\uDCC4";
            label.textContent = key;
            item.dataset.path = itemPath;
            item.addEventListener("click", () => openFile(itemPath, item));
            item.addEventListener("contextmenu", (e) => {
                e.preventDefault();
                e.stopPropagation();
                showFileTreeMenu(e.clientX, e.clientY, { type: 'file', path: itemPath, itemEl: item });
            });
        } else {
            const subVals = {};

            // Gather sub-keys to check if folder has content
            if (val && typeof val === "object") {
                Object.keys(val).filter(k => k !== "__files__").forEach(sk => { subVals[sk] = val[sk]; });
            }

            icon.textContent = "\uD83D\uDCC1";
            label.textContent = key;
            item.classList.add("folder");
            // Only add toggle if folder actually has content
            if (Object.keys(subVals).length > 0) {
                item.addEventListener("click", (e) => {
                    e.stopPropagation();
                    const children = item.nextElementSibling;
                    if (children) {
                        const collapsed = children.classList.toggle("hidden");
                        icon.textContent = collapsed ? "\uD83D\uDCC1" : "\uD83D\uDCC2";
                        saveExpandState(itemPath, !collapsed);
                    }
                });
            }
            item.addEventListener("contextmenu", (e) => {
                e.preventDefault();
                e.stopPropagation();
                showFileTreeMenu(e.clientX, e.clientY, { type: 'folder', path: itemPath, itemEl: item });
            });
        }
        item.appendChild(icon);
        item.appendChild(label);
        container.appendChild(item);

        if (val && typeof val === "object" && !val.file) {
            const subKeys = Object.keys(val).filter(k => k !== "__files__");
            if (subKeys.length > 0) {
                const childContainer = document.createElement("div");
                childContainer.className = "file-tree-children hidden";
                const newBase = basePath ? basePath + "/" + key : key;
                const subNode = {};
                for (const sk of subKeys) subNode[sk] = val[sk];
                renderFileTree(subNode, childContainer, newBase, item);
                container.appendChild(childContainer);
                item.dataset.folder = "true";
                item.dataset.path = itemPath;
            }
        }
    }
}

function resetEditorState(data, path, activeItem, plainText, rawContent, errorMsg) {
    isPlainTextMode = !!plainText;
    if (isPlainTextMode) {
        jsonData = null;
        plainTextContent = rawContent;
        window._rawText = rawContent;
        document.getElementById('plain-text-warning').textContent = errorMsg || 'Invalid JSON format';
    } else {
        jsonData = data;
        plainTextContent = null;
        window._rawText = null;
    }
    currentFilePath = path;
    modified = false;
    clearDiff();
    undoStack.length = 0;
    redoStack.length = 0;
    clearSearchHighlights();
    document.getElementById('search-bar').classList.add('hidden');
    document.getElementById('replace-bar').classList.add('hidden');
    setActiveFileItem(activeItem || null);
    updateUI();
}

/* ── File Tree Context Menu ── */

let ftContextTarget = null; // { type:'file'|'folder'|'empty', path, itemEl }

function showFileTreeMenu(x, y, target) {
    ftContextTarget = target;
    hideContextMenu();
    const menu = document.getElementById('filetree-menu');

    // Show/hide items based on target type
    menu.querySelector('[data-action="ft-rename"]').style.display = 'none';
    menu.querySelector('[data-action="ft-copy"]').style.display = 'none';
    menu.querySelector('[data-action="ft-newfile"]').style.display = 'none';
    menu.querySelector('[data-action="ft-delete"]').style.display = 'none';

    if (target.type === 'file') {
        menu.querySelector('[data-action="ft-rename"]').style.display = '';
        menu.querySelector('[data-action="ft-copy"]').style.display = '';
        menu.querySelector('[data-action="ft-delete"]').style.display = '';
    } else if (target.type === 'folder') {
        menu.querySelector('[data-action="ft-rename"]').style.display = '';
        menu.querySelector('[data-action="ft-newfile"]').style.display = '';
        menu.querySelector('[data-action="ft-delete"]').style.display = '';
    }

    positionAndShowMenu(menu, x, y);
}

function showFileTreeEmptyMenu(x, y) {
    ftContextTarget = { type: 'empty', path: '', itemEl: null };
    hideContextMenu();
    const menu = document.getElementById('filetree-empty-menu');
    positionAndShowMenu(menu, x, y);
}

function positionAndShowMenu(menu, x, y) {
    const mw = menu.offsetWidth;
    const mh = menu.offsetHeight;
    let left = Math.min(x, window.innerWidth - mw - 8);
    let top = y;
    if (top + mh > window.innerHeight - 8) top = Math.max(8, window.innerHeight - mh - 8);
    menu.style.left = Math.max(8, left) + 'px';
    menu.style.top = Math.max(8, top) + 'px';
    menu.classList.remove('hidden');

    menu.querySelectorAll('.menu-item').forEach(item => {
        item.onclick = async (e) => {
            e.stopPropagation();
            const action = item.dataset.action;
            const target = { ...ftContextTarget };
            hideAllMenus();
            try {
                await handleFileTreeAction(target, action);
            } catch (err) {
                console.error('File tree action error:', err);
            }
        };
    });
}

function hideAllMenus() {
    document.getElementById('filetree-menu').classList.add('hidden');
    document.getElementById('filetree-empty-menu').classList.add('hidden');
    hideFileTreeMenu();
}

function hideFileTreeMenu() {
    document.getElementById('filetree-menu').classList.add('hidden');
    document.getElementById('filetree-empty-menu').classList.add('hidden');
    ftContextTarget = null;
}

async function handleFileTreeAction(t, action) {
    if (!t) return;

    if (action === 'fe-newfolder') {
        const name = prompt('New folder name:');
        if (name) await createFolderOp('', name);
        return;
    }
    if (action === 'fe-newfile') {
        const name = prompt('New JSON file name:');
        if (name) {
            const fn = name.endsWith('.json') ? name : name + '.json';
            await createFileOp('', fn);
        }
        return;
    }
    if (action === 'ft-newfile') {
        const name = prompt('New JSON file name in "' + t.path + '":');
        if (name) {
            const fn = name.endsWith('.json') ? name : name + '.json';
            await createFileOp(t.path, fn);
        }
        return;
    }

    if (action === 'ft-rename') {
        startFileTreeRename(t);
    } else if (action === 'ft-copy') {
        const fileName = t.path.split('/').pop();
        const base = fileName.replace(/\.[^.]+$/, '');
        const ext = fileName.match(/\.[^.]+$/)?.[0] || '.json';
        const newName = prompt('Copy as:', base + '_copy' + ext);
        if (newName && newName !== t.path) {
            const resultStr = await copy_file(t.path, newName);
            const result = JSON.parse(resultStr);
            if (result.success) {
                reloadFullTree();
            } else {
                showError(result.error);
            }
        }
    } else if (action === 'ft-delete') {
        const label = t.type === 'folder' ? 'folder "' + t.path + '"' : 'file "' + t.path + '"';
        showConfirm('Delete ' + label + '? This cannot be undone.', async () => {
            const resultStr = await delete_path(t.path);
            const result = JSON.parse(resultStr);
            if (result.success) {
                if (currentFilePath === t.path) {
                    resetEditorState(null, '', null);
                }
                reloadFullTree();
            } else {
                showError(result.error);
            }
        }, 'Delete');
    }
}

let folderExpandState = {};
function restoreExpandState() {
    document.querySelectorAll('#file-tree .file-tree-item.folder').forEach(item => {
        const path = item.dataset.path;
        if (path && folderExpandState[path]) {
            const children = item.nextElementSibling;
            if (children && children.classList.contains('file-tree-children')) {
                children.classList.remove('hidden');
                const icon = item.querySelector('.icon');
                if (icon) icon.textContent = '📂';
            }
        }
    });
}
function saveExpandState(path, expanded) {
    if (expanded) folderExpandState[path] = true;
    else delete folderExpandState[path];
}

async function reloadFullTree() {
    await loadFileTree(99);
    setTimeout(() => restoreExpandState(), 80);
}

async function createFolderOp(parentPath, name) {
    const resultStr = await create_folder(parentPath, name);
    const result = JSON.parse(resultStr);
    if (result.success) {
        await reloadFullTree();
    } else {
        showError(result.error);
    }
}

async function createFileOp(parentPath, name) {
    const resultStr = await create_file(parentPath, name);
    const result = JSON.parse(resultStr);
    if (result.success) {
        await reloadFullTree();
    } else {
        showError(result.error);
    }
}

function startFileTreeRename(target) {
    const item = target.itemEl;
    if (!item) return;
    const label = item.querySelector('.label');
    if (!label) return;
    const oldName = label.textContent;
    const rect = label.getBoundingClientRect();

    const editDiv = document.getElementById('ft-rename-input');
    const input = document.getElementById('ft-rename-text');
    editDiv.style.left = rect.left + 'px';
    editDiv.style.top = rect.top + 'px';
    editDiv.classList.remove('hidden');
    input.value = oldName;
    input.style.width = Math.max(120, rect.width + 16) + 'px';
    input.focus();
    input.select();

    let finishing = false;
    const finish = async () => {
        if (finishing) return;
        finishing = true;
        input.removeEventListener('blur', onBlur);
        input.removeEventListener('keydown', onKey);
        editDiv.classList.add('hidden');
        const newName = input.value.trim();
        if (newName && newName !== oldName) {
            const resultStr = await rename_path(target.path, newName);
            const result = JSON.parse(resultStr);
            if (result.success) {
                if (currentFilePath === target.path) {
                    const parts = target.path.split('/');
                    parts[parts.length - 1] = newName;
                    currentFilePath = parts.join('/');
                    document.getElementById('file-path-display').textContent = currentFilePath;
                }
                reloadFullTree();
            } else {
                showError(result.error);
            }
        }
    };

    const onBlur = () => setTimeout(finish, 100);
    const onKey = (e) => {
        if (e.key === 'Enter') { e.preventDefault(); finish(); }
        else if (e.key === 'Escape') { e.preventDefault(); editDiv.classList.add('hidden'); }
    };

    input.addEventListener('blur', onBlur);
    input.addEventListener('keydown', onKey);
}

/* ── End File Tree Context Menu ── */

function canDiscardCurrentChanges() {
    if (isPlainTextMode) return true;
    return !modified || window.confirm("You have unsaved changes. Load a new file without saving?");
}

function has_unsaved_changes() {
    if (isPlainTextMode) return 0;
    return modified ? 1 : 0;
}

function setupDragAndDrop() {
    window.addEventListener("dragover", (e) => {
        e.preventDefault();
    });
    window.addEventListener("drop", async (e) => {
        e.preventDefault();
        const file = e.dataTransfer && e.dataTransfer.files && e.dataTransfer.files[0];
        if (!file) return;
        if (!file.name.toLowerCase().endsWith(".json")) {
            showError("Please drop a .json file.");
            return;
        }
        if (!canDiscardCurrentChanges()) return;
        try {
            const text = await file.text();
            const data = JSON.parse(text);
            resetEditorState(data, file.name, null);
        } catch (err) {
            showError("Error loading dropped file: " + err.message);
        }
    });
}

function setActiveFileItem(el) {
    document.querySelectorAll(".file-tree-item.active").forEach(e => e.classList.remove("active"));
    if (el) el.classList.add("active");
}

async function openFile(relPath, el) {
    if (!canDiscardCurrentChanges()) return;
    setActiveFileItem(el);
    try {
        const resultStr = await load_file(relPath);
        const result = JSON.parse(resultStr);
        if (!result.success) {
            showError(result.error || "Failed to load file");
            return;
        }
        if (result.invalid_json) {
            resetEditorState(null, relPath, el, true, result.raw_text, result.error);
        } else {
            resetEditorState(result.data, relPath, el, false);
        }
    } catch (err) {
        showError("Error loading file: " + err.message);
    }
}

function updateUI() {
    document.getElementById("file-path-display").textContent = currentFilePath
        ? currentFilePath + (isPlainTextMode ? " (invalid JSON)" : (modified ? " (modified)" : ""))
        : "No file selected";

    const treeContainer = document.getElementById("json-tree");
    const emptyState = document.getElementById("empty-state");
    const plainTextView = document.getElementById("plain-text-view");
    const plainTextEl = document.getElementById("plain-text-content");
    const contextMenu = document.getElementById("context-menu");

    if (isPlainTextMode) {
        treeContainer.classList.add("hidden");
        emptyState.classList.add("hidden");
        plainTextView.classList.remove("hidden");
        plainTextEl.value = window._rawText || "";
        contextMenu.classList.add("hidden");

        // Track edits in plain text mode
        if (!plainTextEl._listenerAttached) {
            plainTextEl._listenerAttached = true;
            plainTextEl.addEventListener("input", () => {
                window._rawText = plainTextEl.value;
                modified = true;
                document.getElementById("btnSave").disabled = false;
                document.getElementById("file-path-display").textContent =
                    currentFilePath + " (invalid JSON, modified)";
            });
        }

        document.getElementById("btnSave").disabled = !currentFilePath;
        document.getElementById("btnSaveAs").disabled = !currentFilePath;
        document.getElementById("btnUndo").disabled = true;
        document.getElementById("btnRedo").disabled = true;
        document.getElementById("btnExpandAll").disabled = true;
        document.getElementById("btnCollapseAll").disabled = true;
        document.getElementById("btnSearch").disabled = true;
        return;
    }

    // JSON mode
    plainTextView.classList.add("hidden");
    treeContainer.classList.remove("hidden");
    document.getElementById("btnUndo").disabled = false;
    document.getElementById("btnRedo").disabled = false;
    document.getElementById("btnExpandAll").disabled = false;
    document.getElementById("btnCollapseAll").disabled = false;
    document.getElementById("btnSearch").disabled = false;
    document.getElementById("btnSave").disabled = !modified || !currentFilePath;
    document.getElementById("btnSaveAs").disabled = !jsonData;

    if (!jsonData) {
        treeContainer.innerHTML = "";
        emptyState.classList.remove("hidden");
        return;
    }
    emptyState.classList.add("hidden");
    treeContainer.innerHTML = "";
    treeContainer.appendChild(renderValue(jsonData, [], undefined, false, -1));
    expandAll();
    requestAnimationFrame(alignIndents);
}

function getValueByPath(data, path) {
    let current = data;
    for (const segment of path) {
        if (current === null || current === undefined) return undefined;
        current = current[segment];
    }
    return current;
}

function setValueByPath(data, path, value) {
    if (path.length === 0) { jsonData = value; return; }
    let current = data;
    for (let i = 0; i < path.length - 1; i++) current = current[path[i]];
    current[path[path.length - 1]] = value;
}

function removeValueByPath(data, path) {
    if (path.length === 0) return;
    let current = data;
    for (let i = 0; i < path.length - 1; i++) current = current[path[i]];
    const lastKey = path[path.length - 1];
    if (Array.isArray(current)) current.splice(lastKey, 1);
    else delete current[lastKey];
}

function duplicateValueByPath(data, path) {
    if (path.length === 0) return null;
    let current = data;
    for (let i = 0; i < path.length - 1; i++) current = current[path[i]];
    const lastKey = path[path.length - 1];
    const value = current[lastKey];
    let newPath;
    if (Array.isArray(current)) {
        const newIdx = current.length;
        current.push(JSON.parse(JSON.stringify(value)));
        newPath = [...path.slice(0, -1), newIdx];
    } else {
        let newKey = String(lastKey) + "_copy";
        let counter = 1;
        while (newKey in current) { counter++; newKey = String(lastKey) + "_copy" + counter; }
        current[newKey] = JSON.parse(JSON.stringify(value));
        newPath = [...path.slice(0, -1), newKey];
    }
    return newPath;
}

function getExpandedState() {
    const expanded = new Set();
    document.querySelectorAll("#json-tree .json-node").forEach(node => {
        const pathStr = node.dataset.path;
        if (!pathStr) return;
        const children = node.querySelector(":scope > .json-children");
        if (children && !children.classList.contains("collapsed")) {
            expanded.add(pathStr);
        }
    });
    return expanded;
}

function restoreExpandedState(state) {
    document.querySelectorAll("#json-tree .json-node").forEach(node => {
        const pathStr = node.dataset.path;
        if (!pathStr) return;
        const children = node.querySelector(":scope > .json-children");
        if (!children) return;
        if (state.has(pathStr)) {
            children.classList.remove("collapsed");
            const lines = node.querySelectorAll(":scope > .json-line");
            const first = lines[0];
            const last = lines[lines.length - 1];
            const toggle = first && first.querySelector(".json-toggle");
            const bracket = first && first.querySelector(".json-bracket");
            if (toggle) toggle.className = "json-toggle expanded";
            if (bracket) {
                const val = getValueByPath(jsonData, JSON.parse(pathStr));
                bracket.textContent = Array.isArray(val) ? "[" : "{";
            }
            if (last) last.style.display = "";
        } else {
            children.classList.add("collapsed");
            const lines = node.querySelectorAll(":scope > .json-line");
            const first = lines[0];
            const last = lines[lines.length - 1];
            const toggle = first && first.querySelector(".json-toggle");
            const bracket = first && first.querySelector(".json-bracket");
            if (toggle) toggle.className = "json-toggle collapsed";
            if (bracket) {
                const val = getValueByPath(jsonData, JSON.parse(pathStr));
                if (Array.isArray(val)) bracket.textContent = "[" + val.length + " items]";
                else if (val && typeof val === "object") bracket.textContent = "{" + Object.keys(val).length + " keys}";
                else bracket.textContent = "{...}";
            }
            if (last) last.style.display = "none";
        }
    });
    requestAnimationFrame(alignIndents);
}

function renderValue(val, path, key, isArrayElement, index) {
    const container = document.createElement("div");
    container.className = "json-node";

    const isObj = val !== null && typeof val === "object";

    if (isObj) {
        const isArray = Array.isArray(val);
        const keys = isArray ? val.map((_, i) => i) : Object.keys(val);
        const len = keys.length;

        const line = document.createElement("div");
        line.className = "json-line";
        line.dataset.path = JSON.stringify(path);
        line.addEventListener("click", () => { lastActivePath = path; });

        const toggle = document.createElement("span");
        toggle.className = "json-toggle expanded";
        if (len === 0) toggle.classList.add("hidden");
        toggle.addEventListener("click", (e) => {
            e.stopPropagation();
            toggleNode(container);
        });
        line.appendChild(toggle);

        if (key !== undefined && key !== null) {
            const keySpan = makeKeySpan(key, isArrayElement, index, path);
            if (keySpan) line.appendChild(keySpan);
        }

        const bracketOpen = document.createElement("span");
        bracketOpen.className = "json-bracket";
        bracketOpen.textContent = isArray ? "[" : "{";
        line.appendChild(bracketOpen);

        if (len > 0) {
            const info = document.createElement("span");
            info.style.cssText = "color:#888;font-size:12px;margin-left:6px;";
            info.textContent = "// " + len + (isArray ? " items" : " keys");
            line.appendChild(info);
        }

        container.appendChild(line);

        const children = document.createElement("div");
        children.className = "json-children";
        for (let i = 0; i < len; i++) {
            const k = keys[i];
            const childNode = renderValue(val[k], [...path, k], String(k), isArray, isArray ? k : -1);
            children.appendChild(childNode);
        }
        container.appendChild(children);

        const closeLine = document.createElement("div");
        closeLine.className = "json-line";
        const closeSpan = document.createElement("span");
        closeSpan.className = "json-bracket";
        closeSpan.textContent = isArray ? "]" : "}";
        closeLine.appendChild(closeSpan);
        container.appendChild(closeLine);

        container.dataset.path = JSON.stringify(path);
        setupContextMenu(container, path, key, isArrayElement, val);

    } else {
        const line = document.createElement("div");
        line.className = "json-line";
        line.dataset.path = JSON.stringify(path);
        line.addEventListener("click", () => { lastActivePath = path; });
        const keySpan = makeKeySpan(key, isArrayElement, index, path);
        if (keySpan) line.appendChild(keySpan);
        const valueSpan = makeValueSpan(val, path);
        line.appendChild(valueSpan);
        container.appendChild(line);
        container.dataset.path = JSON.stringify(path);
        setupContextMenu(container, path, key, isArrayElement, val);
    }

    return container;
}

function toggleNode(container) {
    const children = container.querySelector(":scope > .json-children");
    if (!children) return;
    const isCollapsed = children.classList.toggle("collapsed");
    const lines = container.querySelectorAll(":scope > .json-line");
    const first = lines[0];
    const last = lines[lines.length - 1];
    const toggle = first && first.querySelector(".json-toggle");
    const bracket = first && first.querySelector(".json-bracket");
    if (toggle) toggle.className = isCollapsed ? "json-toggle collapsed" : "json-toggle expanded";

    if (isCollapsed) {
        if (bracket) {
            const pathStr = container.dataset.path;
            if (pathStr) {
                try {
                    const val = getValueByPath(jsonData, JSON.parse(pathStr));
                    if (Array.isArray(val)) bracket.textContent = "[" + val.length + " items]";
                    else if (val && typeof val === "object") bracket.textContent = "{" + Object.keys(val).length + " keys}";
                    else bracket.textContent = "{...}";
                } catch(e) { bracket.textContent = "{...}"; }
            }
        }
        if (last) last.style.display = "none";
    } else {
        if (bracket) {
            const val = getValueByPath(jsonData, JSON.parse(container.dataset.path));
            bracket.textContent = Array.isArray(val) ? "[" : "{";
        }
        if (last) {
            last.style.display = "";
            requestAnimationFrame(alignIndents);
        }
    }
}

function makeKeySpan(key, isArrayElement, index, path) {
    if (key === undefined || key === null) return null;
    const span = document.createElement("span");
    span.className = "json-key";
    if (isArrayElement && index >= 0) {
        const idx = document.createElement("span");
        idx.className = "json-array-index";
        idx.textContent = "[" + index + "]";
        span.appendChild(idx);
        span.appendChild(document.createTextNode(": "));
    } else {
        span.dataset.path = JSON.stringify(path);
        span.dataset.rawText = String(key);
        span.dataset.targetType = "key";
        appendQuote(span);
        span.appendChild(document.createTextNode(key));
        appendQuote(span);
        appendColon(span);
    }
    span.addEventListener("dblclick", (e) => {
        e.stopPropagation();
        if (isArrayElement) return;
        startEditKey(span, key, path);
    });
    return span;
}

function makeValueSpan(val, path) {
    const span = document.createElement("span");
    span.className = "json-value";
    span.dataset.path = JSON.stringify(path);
    span.dataset.rawText = val === null ? "null" : String(val);
    span.dataset.targetType = "value";
    if (val === null) {
        span.textContent = "null";
        span.classList.add("type-null");
        span.dataset.valueType = "null";
    } else if (typeof val === "string") {
        span.dataset.rawText = val;
        span.dataset.valueType = "string";
        appendQuote(span);
        span.appendChild(document.createTextNode(val));
        appendQuote(span);
        span.classList.add("type-string");
    } else if (typeof val === "number") {
        span.textContent = String(val);
        span.classList.add("type-number");
        span.dataset.valueType = "number";
    } else if (typeof val === "boolean") {
        span.textContent = val ? "true" : "false";
        span.classList.add("type-boolean");
        span.dataset.valueType = "boolean";
    } else {
        span.textContent = String(val);
        span.classList.add("type-undefined");
        span.dataset.valueType = "undefined";
    }
    span.addEventListener("dblclick", (e) => {
        e.stopPropagation();
        startEditValue(span, val, path);
    });
    return span;
}

function startEditKey(span, oldKey, path) {
    if (span.classList.contains("editing")) return;
    span.classList.add("editing");
    const input = document.createElement("input");
    input.className = "json-edit-input";
    input.type = "text";
    input.value = oldKey;
    input.style.width = Math.max(60, oldKey.length * 8 + 24) + "px";
    span.innerHTML = "";
    span.appendChild(input);
    input.focus();
    input.select();
        const finish = () => {
            let newKey = input.value.trim();
            if (newKey.length >= 2 && newKey.startsWith('"') && newKey.endsWith('"')) {
                try { const p = JSON.parse(newKey); if (typeof p === 'string') newKey = p; } catch(e) {}
            }
            span.classList.remove("editing");
            if (newKey && newKey !== oldKey) {
                const parent = getValueByPath(jsonData, path.slice(0, -1));
                if (parent && typeof parent === "object" && !Array.isArray(parent)) {
                    pushSnapshot();
                    const newPath = [...path.slice(0, -1), newKey];
                    recordChange('modified', newPath, { oldVal: parent[oldKey], newVal: parent[oldKey], oldKey });
                    parent[newKey] = parent[oldKey];
                    delete parent[oldKey];
                    markModified();
                    const state = getExpandedState();
                    rerenderJson();
                    restoreExpandedState(state);
                    return;
                }
            }
            rerenderJson();
        };
    input.addEventListener("blur", finish);
    input.addEventListener("keydown", (e) => {
        if (e.key === "Enter") { e.preventDefault(); input.blur(); }
        else if (e.key === "Escape") { e.preventDefault(); span.classList.remove("editing"); rerenderJson(); }
        e.stopPropagation();
    });
}

function startEditValue(span, oldVal, path) {
    if (span.classList.contains("editing")) return;
    span.classList.add("editing");
    const input = document.createElement("input");
    input.className = "json-edit-input";
    input.type = "text";
    let strVal;
    if (oldVal === null) strVal = "null";
    else if (typeof oldVal === "boolean") strVal = String(oldVal);
    else if (typeof oldVal === "string") strVal = oldVal;
    else if (typeof oldVal === "number") strVal = String(oldVal);
    else strVal = String(oldVal);
    input.value = strVal;
    input.style.width = Math.max(60, strVal.length * 8 + 24) + "px";
    span.innerHTML = "";
    span.appendChild(input);
    input.focus();
    input.select();
    const finish = () => {
        const raw = input.value;
        span.classList.remove("editing");
        let newVal;
        if (raw.length >= 2 && raw.startsWith('"') && raw.endsWith('"')) {
            try { newVal = JSON.parse(raw); } catch(e) { newVal = raw; }
        } else if (raw === "null") newVal = null;
        else if (raw === "true") newVal = true;
        else if (raw === "false") newVal = false;
        else if (!isNaN(raw) && raw.trim() !== "") newVal = Number(raw);
        else newVal = raw;
        pushSnapshot();
        recordChange('modified', path, { oldVal, newVal });
        setValueByPath(jsonData, path, newVal);
        markModified();
        const state = getExpandedState();
        rerenderJson();
        restoreExpandedState(state);
    };
    input.addEventListener("blur", finish);
    input.addEventListener("keydown", (e) => {
        if (e.key === "Enter") { e.preventDefault(); input.blur(); }
        else if (e.key === "Escape") { e.preventDefault(); span.classList.remove("editing"); rerenderJson(); }
        e.stopPropagation();
    });
}

function expandAll() {
    document.querySelectorAll("#json-tree .json-children.collapsed").forEach(el => el.classList.remove("collapsed"));
    document.querySelectorAll("#json-tree .json-toggle.collapsed").forEach(el => el.className = "json-toggle expanded");
    document.querySelectorAll("#json-tree .json-node").forEach(node => {
        const lines = node.querySelectorAll(":scope > .json-line");
        const first = lines[0];
        const last = lines[lines.length - 1];
        const bracket = first && first.querySelector(".json-bracket");
        if (bracket) {
            try {
                const val = getValueByPath(jsonData, JSON.parse(node.dataset.path));
                bracket.textContent = Array.isArray(val) ? "[" : "{";
            } catch(e) { bracket.textContent = "{"; }
        }
        if (last) last.style.display = "";
    });
    requestAnimationFrame(alignIndents);
}

function collapseAll() {
    document.querySelectorAll("#json-tree .json-children").forEach(el => el.classList.add("collapsed"));
    document.querySelectorAll("#json-tree .json-toggle.expanded").forEach(el => el.className = "json-toggle collapsed");
    document.querySelectorAll("#json-tree .json-node").forEach(node => {
        const lines = node.querySelectorAll(":scope > .json-line");
        const first = lines[0];
        const last = lines[lines.length - 1];
        const bracket = first && first.querySelector(".json-bracket");
        if (bracket) {
            const pathStr = node.dataset.path;
            if (pathStr) {
                try {
                    const val = getValueByPath(jsonData, JSON.parse(pathStr));
                    if (Array.isArray(val)) bracket.textContent = "[" + val.length + " items]";
                    else if (val && typeof val === "object") bracket.textContent = "{" + Object.keys(val).length + " keys}";
                    else bracket.textContent = "{...}";
                } catch(e) { bracket.textContent = "{...}"; }
            }
        }
        if (last && last !== first) last.style.display = "none";
    });
}

let cachedChWidth = 0;

function getChWidth() {
    if (cachedChWidth > 0) return cachedChWidth;
    const temp = document.createElement("span");
    temp.textContent = "00000";
    temp.style.cssText = "position:fixed;visibility:hidden;font-size:inherit;font-family:inherit;";
    document.body.appendChild(temp);
    cachedChWidth = temp.getBoundingClientRect().width / 5;
    document.body.removeChild(temp);
    return cachedChWidth;
}

let addDialogState = null;

function showAddChildDialog(path, isArray) {
    const dialog = document.getElementById("add-dialog");
    const keyGroup = document.getElementById("add-key-group");
    const keyInput = document.getElementById("add-key-input");
    const valueInput = document.getElementById("add-value-input");
    const errorDiv = document.getElementById("add-error");
    keyInput.value = "";
    valueInput.value = "";
    errorDiv.classList.add("hidden");
    keyGroup.style.display = isArray ? "none" : "";
    dialog.classList.remove("hidden");
    addDialogState = { path, isArray };
    if (isArray) {
        setTimeout(() => valueInput.focus(), 100);
    } else {
        setTimeout(() => keyInput.focus(), 100);
    }
}

function hideAddChildDialog() {
    document.getElementById("add-dialog").classList.add("hidden");
    addDialogState = null;
}

function doAddChild() {
    if (!addDialogState) return;
    const { path, isArray } = addDialogState;
    const keyInput = document.getElementById("add-key-input");
    const valueInput = document.getElementById("add-value-input");
    const errorDiv = document.getElementById("add-error");
    const raw = valueInput.value.trim();
    let parsed;
    try {
        parsed = JSON.parse(raw);
    } catch (e) {
        errorDiv.textContent = "Invalid JSON: " + e.message;
        errorDiv.classList.remove("hidden");
        return;
    }
    let newKey = "";
    if (!isArray) {
        newKey = keyInput.value.trim();
        if (newKey.length >= 2 && newKey.startsWith('"') && newKey.endsWith('"')) {
            try { const p = JSON.parse(newKey); if (typeof p === 'string') newKey = p; } catch(e) {}
        }
        if (!newKey) {
            errorDiv.textContent = "Key cannot be empty";
            errorDiv.classList.remove("hidden");
            return;
        }
        const parent = path.length === 0 ? jsonData : getValueByPath(jsonData, path);
        if (parent && typeof parent === "object" && newKey in parent) {
            errorDiv.textContent = "Key '" + newKey + "' already exists";
            errorDiv.classList.remove("hidden");
            return;
        }
    }
    errorDiv.classList.add("hidden");
    pushSnapshot();
    const expandedState = getExpandedState();
    const parent = path.length === 0 ? jsonData : getValueByPath(jsonData, path);
    let addedPath;
    if (isArray) {
        const newIdx = parent.length;
        parent.push(parsed);
        addedPath = [...path, newIdx];
    } else {
        parent[newKey] = parsed;
        addedPath = [...path, newKey];
    }
    recordChange('added', addedPath, { value: parsed });
    markModified();
    hideAddChildDialog();
    rerenderJson();
    restoreExpandedState(expandedState);
}

function rerenderJson() {
    if (!jsonData) return;
    const treeContainer = document.getElementById("json-tree");
    treeContainer.innerHTML = "";
    treeContainer.appendChild(renderValue(jsonData, [], undefined, false, -1));
    requestAnimationFrame(alignIndents);
    requestAnimationFrame(applyDiffMarkers);
}

function alignIndents() {
    const indent = 2 * getChWidth();
    const nodes = document.querySelectorAll("#json-tree .json-node");
    for (const node of nodes) {
        const firstLine = node.querySelector(":scope > .json-line");
        if (!firstLine) continue;
        const keySpan = firstLine.querySelector(".json-key");
        const bracketSpan = firstLine.querySelector(".json-bracket");
        const children = node.querySelector(":scope > .json-children");
        const allLines = node.querySelectorAll(":scope > .json-line");
        const lastLine = allLines.length > 1 ? allLines[allLines.length - 1] : null;
        if (!bracketSpan) continue;
        const lr = firstLine.getBoundingClientRect();
        const br = bracketSpan.getBoundingClientRect();
        const off = br.left - lr.left;
        if (children) children.style.marginLeft = (off + indent) + "px";
        if (lastLine && lastLine !== firstLine && lastLine.style.display !== "none") {
            lastLine.style.paddingLeft = off + "px";
        }
    }
}

function markModified() {
    if (!modified) {
        modified = true;
        document.getElementById("btnSave").disabled = false;
        document.getElementById("file-path-display").textContent = currentFilePath + " (modified)";
    }
}

function setupContextMenu(container, path, key, isArrayElement, val) {
    container.addEventListener("contextmenu", (e) => {
        if (isPlainTextMode) return;
        e.preventDefault();
        e.stopPropagation();
        const isObj = val !== null && typeof val === "object";
        showContextMenu(e.clientX, e.clientY, path, key, isArrayElement, val, isObj);
    });
}

function showContextMenu(x, y, path, key, isArrayElement, val, isObject) {
    if (isPlainTextMode) return;
    lastActivePath = path;
    const menu = document.getElementById("context-menu");
    menu.classList.remove("hidden");
    menu.style.left = "";
    menu.style.top = "";
    const mw = menu.offsetWidth;
    const mh = menu.offsetHeight;
    let left = Math.min(x, window.innerWidth - mw - 8);
    let top = y;
    if (top + mh > window.innerHeight - 8) {
        top = Math.max(8, window.innerHeight - mh - 8);
    }
    if (top + mh > window.innerHeight - 8) {
        top = Math.max(8, y - mh - 8);
    }
    menu.style.left = Math.max(8, left) + "px";
    menu.style.top = Math.max(8, top) + "px";
    menu._state = { path, key, isArrayElement, val, isObject };
    const addItem = menu.querySelector('[data-action="add-child"]');
    if (addItem) addItem.style.display = isObject ? "" : "none";
    document.querySelectorAll("#context-menu .menu-item").forEach(item => {
        const action = item.dataset.action;
        item.onclick = () => handleContextMenu(action, menu._state);
    });
}

function hideContextMenu() {
    document.getElementById("context-menu").classList.add("hidden");
}

function handleContextMenu(action, state) {
    hideContextMenu();
    switch (action) {
        case "add-child":
            if (state.val && typeof state.val === "object") {
                showAddChildDialog(state.path, Array.isArray(state.val));
            }
            break;
        case "copy-key":
            if (state.key !== undefined && state.key !== null && state.key !== "") {
                navigator.clipboard.writeText(state.key).catch(() => {});
            }
            break;
        case "copy-value": {
            let valStr;
            if (state.val === null) valStr = "null";
            else if (typeof state.val === "object") valStr = JSON.stringify(state.val, null, 2);
            else valStr = String(state.val);
            navigator.clipboard.writeText(valStr).catch(() => {});
            break;
        }
        case "duplicate":
            if (state.path.length > 0) {
                pushSnapshot();
                const expandedState = getExpandedState();
                const newPath = duplicateValueByPath(jsonData, state.path);
                if (newPath) recordChange('added', newPath, { value: getValueByPath(jsonData, newPath) });
                markModified();
                rerenderJson();
                restoreExpandedState(expandedState);
            }
            break;
        case "delete":
            if (state.path.length > 0) {
                showConfirm("Delete this node?", () => {
                    try {
                        pushSnapshot();
                        const expandedState = getExpandedState();
                        const parentPath = state.path.slice(0, -1);
                        const parent = state.path.length === 1 ? jsonData : getValueByPath(jsonData, parentPath);
                        const lastKey = state.path[state.path.length - 1];
                        let position;
                        if (Array.isArray(parent)) {
                            position = parseInt(lastKey);
                        } else {
                            position = Object.keys(parent).indexOf(lastKey);
                        }
                        recordChange('deleted', state.path, {
                            value: state.val,
                            parentPath,
                            position,
                            key: state.key,
                            isArrayElement: state.isArrayElement
                        });
                        removeValueByPath(jsonData, state.path);
                        markModified();
                        rerenderJson();
                        restoreExpandedState(expandedState);
                    } catch(e) {
                        console.error("Delete callback error:", e);
                    }
                });
            }
            break;
        case "replace-scope": {
            lastActivePath = state.path;
            let scope;
            if (state.val !== null && typeof state.val === "object") {
                scope = { type: 'node', path: state.path };
            } else {
                scope = { type: 'value', path: state.path };
            }
            showReplaceBar(scope);
            break;
        }
        case "replace-global":
            showReplaceBar({ type: 'root' });
            break;
    }
}

function showConfirm(message, callback, okText) {
    document.getElementById("confirm-message").textContent = message;
    document.getElementById("confirm-ok").textContent = okText || "OK";
    document.getElementById("confirm-dialog").classList.remove("hidden");
    confirmCallback = callback;
}

function hideConfirm() {
    document.getElementById("confirm-dialog").classList.add("hidden");
    document.getElementById("confirm-ok").textContent = "OK";
    confirmCallback = null;
}



function showError(msg) {
    showConfirm(msg, () => {});
    document.getElementById("confirm-ok").textContent = "OK";
}

async function saveFile() {
    if (!currentFilePath) return;
    if (!isPlainTextMode && (!modified || !jsonData)) return;
    try {
        let resultStr;
        if (isPlainTextMode) {
            const textarea = document.getElementById("plain-text-content");
            const rawText = textarea ? textarea.value : (window._rawText || "");
            resultStr = await save_raw_file(currentFilePath, rawText);
        } else {
            const content = JSON.stringify(jsonData, null, 2);
            resultStr = await save_file(currentFilePath, content);
        }
        const result = JSON.parse(resultStr);
        if (result.success) {
            modified = false;
            if (!isPlainTextMode) {
                clearDiff();
                rerenderJson();
            }
            document.getElementById("btnSave").disabled = true;
            document.getElementById("file-path-display").textContent = currentFilePath
                ? currentFilePath + (isPlainTextMode ? " (invalid JSON)" : "")
                : "No file selected";
        } else {
            showError("Save failed: " + (result.error || "unknown error"));
        }
    } catch (err) {
        showError("Error saving file: " + err.message);
    }
}

function showSaveAsDialog() {
    if (!jsonData && !isPlainTextMode) return;
    document.getElementById("save-as-path").value = currentFilePath || "untitled.json";
    document.getElementById("save-as-dialog").classList.remove("hidden");
    setTimeout(() => document.getElementById("save-as-path").focus(), 100);
}

async function onBrowseSaveAs() {
    try {
        const path = await show_save_dialog();
        if (path) {
            document.getElementById("save-as-path").value = path;
        }
    } catch (e) {
        console.error("Save dialog error:", e);
    }
}

function hideSaveAsDialog() {
    document.getElementById("save-as-dialog").classList.add("hidden");
}

async function doSaveAs() {
    const filePath = document.getElementById("save-as-path").value.trim();
    if (!filePath) return;
    try {
        let resultStr;
        if (isPlainTextMode) {
            const textarea = document.getElementById("plain-text-content");
            const rawText = textarea ? textarea.value : (window._rawText || "");
            resultStr = await save_raw_file(filePath, rawText);
        } else {
            const content = JSON.stringify(jsonData, null, 2);
            resultStr = await save_file_as(filePath, content);
        }
        const result = JSON.parse(resultStr);
        if (result.success) {
            hideSaveAsDialog();
            modified = false;
            if (!isPlainTextMode) {
                clearDiff();
                rerenderJson();
            }
            currentFilePath = result.path || filePath;
            document.getElementById("btnSave").disabled = true;
            document.getElementById("file-path-display").textContent = currentFilePath;
            await loadFileTree();
        } else {
            showError("Save failed: " + (result.error || "unknown error"));
        }
    } catch (err) {
        showError("Error saving file: " + err.message);
    }
}

document.addEventListener("DOMContentLoaded", init);
