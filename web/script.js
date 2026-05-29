let jsonData = null;
let currentFilePath = "";
let modified = false;
let confirmCallback = null;

async function init() {
    await loadFileTree();

    document.getElementById("btnRefresh").addEventListener("click", loadFileTree);
    document.getElementById("btnExpandAll").addEventListener("click", expandAll);
    document.getElementById("btnCollapseAll").addEventListener("click", collapseAll);
    document.getElementById("btnSave").addEventListener("click", saveFile);
    document.getElementById("btnSaveAs").addEventListener("click", showSaveAsDialog);

    document.getElementById("confirm-ok").addEventListener("click", () => {
        hideConfirm();
        if (confirmCallback) confirmCallback();
        confirmCallback = null;
    });
    document.getElementById("confirm-cancel").addEventListener("click", hideConfirm);

    document.getElementById("save-as-ok").addEventListener("click", doSaveAs);
    document.getElementById("save-as-cancel").addEventListener("click", hideSaveAsDialog);
    document.getElementById("save-as-path").addEventListener("keydown", (e) => {
        if (e.key === "Enter") doSaveAs();
    });

    document.addEventListener("click", hideContextMenu);
    document.addEventListener("contextmenu", (e) => e.preventDefault());
    document.addEventListener("keydown", (e) => {
        if (e.key === "Escape") {
            if (!document.getElementById("context-menu").classList.contains("hidden")) {
                hideContextMenu();
            }
        }
    });

    window.addEventListener("beforeunload", (e) => {
        if (modified) {
            e.preventDefault();
            e.returnValue = "";
        }
    });
}

async function loadFileTree() {
    try {
        const treeStr = await get_file_tree();
        const tree = JSON.parse(treeStr);
        renderFileTree(tree, document.getElementById("file-tree"), "", null);
    } catch (err) {
        console.error("Failed to load file tree:", err);
    }
}

function renderFileTree(node, container, basePath, parentItem) {
    container.innerHTML = "";
    if (typeof node !== "object" || node === null) return;
    const keys = Object.keys(node).sort();
    for (const key of keys) {
        const val = node[key];
        if (key === "__files__") {
            renderFileTree(val, container, basePath, parentItem);
            continue;
        }
        const item = document.createElement("div");
        item.className = "file-tree-item";
        const icon = document.createElement("span");
        icon.className = "icon";
        const label = document.createElement("span");
        label.className = "label";

        if (val && typeof val === "object" && val.file) {
            icon.textContent = "\uD83D\uDCC4";
            label.textContent = key;
            const filePath = basePath ? basePath + "/" + key : key;
            item.dataset.path = filePath;
            item.addEventListener("click", () => openFile(filePath, item));
        } else {
            icon.textContent = "\uD83D\uDCC1";
            label.textContent = key;
            item.classList.add("folder");
            item.addEventListener("click", (e) => {
                e.stopPropagation();
                const children = item.nextElementSibling;
                if (children) {
                    children.classList.toggle("hidden");
                    icon.textContent = children.classList.contains("hidden") ? "\uD83D\uDCC1" : "\uD83D\uDCC2";
                }
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
            }
        }
    }
}

function setActiveFileItem(el) {
    document.querySelectorAll(".file-tree-item.active").forEach(e => e.classList.remove("active"));
    if (el) el.classList.add("active");
}

async function openFile(relPath, el) {
    setActiveFileItem(el);
    try {
        const resultStr = await load_file(relPath);
        const result = JSON.parse(resultStr);
        if (!result.success) {
            showError(result.error || "Failed to load file");
            return;
        }
        jsonData = result.data;
        currentFilePath = relPath;
        modified = false;
        updateUI();
    } catch (err) {
        showError("Error loading file: " + err.message);
    }
}

function updateUI() {
    document.getElementById("file-path-display").textContent = currentFilePath || "No file selected";
    document.getElementById("btnSave").disabled = !modified || !currentFilePath;
    document.getElementById("btnSaveAs").disabled = !jsonData;
    const treeContainer = document.getElementById("json-tree");
    const emptyState = document.getElementById("empty-state");
    if (!jsonData) {
        treeContainer.innerHTML = "";
        emptyState.classList.remove("hidden");
        return;
    }
    emptyState.classList.add("hidden");
    treeContainer.innerHTML = "";
    treeContainer.appendChild(renderValue(jsonData, [], undefined, false, -1));
    expandAll();
    requestAnimationFrame(alignCloseBrackets);
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
    if (path.length === 0) return;
    let current = data;
    for (let i = 0; i < path.length - 1; i++) current = current[path[i]];
    const lastKey = path[path.length - 1];
    const value = current[lastKey];
    if (Array.isArray(current)) {
        current.push(JSON.parse(JSON.stringify(value)));
    } else {
        let newKey = String(lastKey) + "_copy";
        let counter = 1;
        while (newKey in current) { counter++; newKey = String(lastKey) + "_copy" + counter; }
        current[newKey] = JSON.parse(JSON.stringify(value));
    }
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
    requestAnimationFrame(alignCloseBrackets);
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

        if (key !== undefined && key !== null) {
            const keySpan = makeKeySpan(key, isArrayElement, index, path);
            if (keySpan) line.appendChild(keySpan);
        }

        const toggle = document.createElement("span");
        toggle.className = "json-toggle expanded";
        if (len === 0) toggle.classList.add("hidden");
        toggle.addEventListener("click", (e) => {
            e.stopPropagation();
            toggleNode(container);
        });
        line.appendChild(toggle);

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
        const closeToggle = document.createElement("span");
        closeToggle.className = "json-toggle hidden";
        closeToggle.style.visibility = "hidden";
        closeToggle.textContent = " ";
        closeLine.appendChild(closeToggle);
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
        const toggle = document.createElement("span");
        toggle.className = "json-toggle hidden";
        toggle.style.visibility = "hidden";
        toggle.textContent = " ";
        line.appendChild(toggle);
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
            requestAnimationFrame(alignCloseBrackets);
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
        const q1 = document.createElement("span");
        q1.style.color = "#888"; q1.textContent = '"';
        span.appendChild(q1);
        span.appendChild(document.createTextNode(key));
        const q2 = document.createElement("span");
        q2.style.color = "#888"; q2.textContent = '"';
        span.appendChild(q2);
        const colon = document.createElement("span");
        colon.className = "json-colon";
        colon.textContent = ": ";
        span.appendChild(colon);
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
    if (val === null) {
        span.textContent = "null";
        span.classList.add("type-null");
    } else if (typeof val === "string") {
        const q1 = document.createElement("span");
        q1.style.color = "#888"; q1.textContent = '"';
        span.appendChild(q1);
        span.appendChild(document.createTextNode(val));
        const q2 = document.createElement("span");
        q2.style.color = "#888"; q2.textContent = '"';
        span.appendChild(q2);
        span.classList.add("type-string");
    } else if (typeof val === "number") {
        span.textContent = String(val);
        span.classList.add("type-number");
    } else if (typeof val === "boolean") {
        span.textContent = val ? "true" : "false";
        span.classList.add("type-boolean");
    } else {
        span.textContent = String(val);
        span.classList.add("type-undefined");
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
        const newKey = input.value.trim();
        span.classList.remove("editing");
        if (newKey && newKey !== oldKey) {
            const parent = getValueByPath(jsonData, path.slice(0, -1));
            if (parent && typeof parent === "object" && !Array.isArray(parent)) {
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
        if (raw === "null") newVal = null;
        else if (raw === "true") newVal = true;
        else if (raw === "false") newVal = false;
        else if (!isNaN(raw) && raw.trim() !== "") newVal = Number(raw);
        else newVal = raw;
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
    requestAnimationFrame(alignCloseBrackets);
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
        if (last) last.style.display = "none";
    });
}

function rerenderJson() {
    if (!jsonData) return;
    const treeContainer = document.getElementById("json-tree");
    treeContainer.innerHTML = "";
    treeContainer.appendChild(renderValue(jsonData, [], undefined, false, -1));
    requestAnimationFrame(alignCloseBrackets);
}

function alignCloseBrackets() {
    const nodes = document.querySelectorAll("#json-tree .json-node");
    for (const node of nodes) {
        const pathStr = node.dataset.path;
        if (!pathStr) continue;
        try {
            const val = JSON.parse(pathStr);
        } catch(e) { continue; }
        const lines = node.querySelectorAll(":scope > .json-line");
        if (lines.length < 2) continue;
        const first = lines[0];
        const last = lines[lines.length - 1];
        if (!first || !last || last.style.display === "none") continue;
        const bracketSpan = first.querySelector(".json-bracket");
        if (!bracketSpan) continue;
        const br = bracketSpan.getBoundingClientRect();
        const lr = first.getBoundingClientRect();
        const offset = br.left - lr.left;
        last.style.paddingLeft = offset + "px";
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
        e.preventDefault();
        e.stopPropagation();
        const isObj = val !== null && typeof val === "object";
        showContextMenu(e.clientX, e.clientY, path, key, isArrayElement, val, isObj);
    });
}

function showContextMenu(x, y, path, key, isArrayElement, val, isObject) {
    const menu = document.getElementById("context-menu");
    menu.style.left = Math.min(x, window.innerWidth - 170) + "px";
    menu.style.top = Math.min(y, window.innerHeight - 150) + "px";
    menu.classList.remove("hidden");
    menu._state = { path, key, isArrayElement, val, isObject };
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
                const expandedState = getExpandedState();
                duplicateValueByPath(jsonData, state.path);
                markModified();
                rerenderJson();
                restoreExpandedState(expandedState);
            }
            break;
        case "delete":
            if (state.path.length > 0) {
                showConfirm("Delete this node?", () => {
                    const expandedState = getExpandedState();
                    removeValueByPath(jsonData, state.path);
                    markModified();
                    rerenderJson();
                    restoreExpandedState(expandedState);
                });
            }
            break;
    }
}

function showConfirm(message, callback) {
    document.getElementById("confirm-message").textContent = message;
    document.getElementById("confirm-ok").textContent = "OK";
    document.getElementById("confirm-dialog").classList.remove("hidden");
    confirmCallback = callback;
}

function hideConfirm() {
    document.getElementById("confirm-dialog").classList.add("hidden");
    confirmCallback = null;
}

function showError(msg) {
    showConfirm(msg, () => {});
    document.getElementById("confirm-ok").textContent = "OK";
}

async function saveFile() {
    if (!modified || !currentFilePath || !jsonData) return;
    try {
        const content = JSON.stringify(jsonData, null, 2);
        const resultStr = await save_file(currentFilePath, content);
        const result = JSON.parse(resultStr);
        if (result.success) {
            modified = false;
            document.getElementById("btnSave").disabled = true;
            document.getElementById("file-path-display").textContent = currentFilePath;
        } else {
            showError("Save failed: " + (result.error || "unknown error"));
        }
    } catch (err) {
        showError("Error saving file: " + err.message);
    }
}

function showSaveAsDialog() {
    if (!jsonData) return;
    document.getElementById("save-as-path").value = currentFilePath || "untitled.json";
    document.getElementById("save-as-dialog").classList.remove("hidden");
    setTimeout(() => document.getElementById("save-as-path").focus(), 100);
}

function hideSaveAsDialog() {
    document.getElementById("save-as-dialog").classList.add("hidden");
}

async function doSaveAs() {
    const filePath = document.getElementById("save-as-path").value.trim();
    if (!filePath) return;
    try {
        const content = JSON.stringify(jsonData, null, 2);
        const resultStr = await save_file_as(filePath, content);
        const result = JSON.parse(resultStr);
        if (result.success) {
            hideSaveAsDialog();
            modified = false;
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
