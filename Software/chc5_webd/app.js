// SPDX-License-Identifier: CC-BY-NC-ND-4.0
/*
 * app.js - vanilla SPA for the CHC5 camera web UI
 *
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

'use strict';

const ICONS = {
    desktop:  '<path d="M3 5 H29 V21 H3 Z" /><path d="M12 25 L13 21 M20 25 L19 21 M11 25 H21" />',

    settings: '<circle cx="16" cy="16" r="4" /><path d="M16 3 V7 M16 25 V29 M3 16 H7 M25 16 H29 M6 6 L9 9 M23 23 L26 26 M26 6 L23 9 M6 26 L9 23" />',

    bell:     '<path d="M7 22 C9 22 9 17 9 13 C9 8 12 5 16 5 C20 5 23 8 23 13 C23 17 23 22 25 22 Z M13 25 C13 27 14 28 16 28 C18 28 19 27 19 25 M16 2 V5" />',

    user:     '<circle cx="16" cy="11" r="5" /><path d="M5 28 C5 22 10 19 16 19 C22 19 27 22 27 28" />',

    import:   '<path d="M16 4 V20 M10 14 L16 20 L22 14 M5 24 H27 V28 H5 Z" />',

    inbox:    '<path d="M4 6 H28 V26 H4 Z M4 18 H11 L13 21 H19 L21 18 H28" />',

    reload:   '<path d="M27 16 A 11 11 0 1 0 16 27" /><path d="M27 11 V16 H22" /><path d="M5 16 A 11 11 0 1 0 16 5" /><path d="M5 21 V16 H10" />',

    refresh:  '<path d="M27 16 A 11 11 0 1 0 16 27" /><path d="M27 11 V16 H22" />',
    clock:    '<circle cx="16" cy="16" r="11" /><path d="M16 16 V9 M16 16 L21 19" />',

    save:     '<path d="M5 5 H22 L27 10 V27 H5 Z" /><path d="M9 5 H21 V12 H9 Z" /><path d="M9 18 H23 V27 H9 Z" />',

    'ellipsis-horizontal': '<circle cx="8"  cy="16" r="2" fill="currentColor" /><circle cx="16" cy="16" r="2" fill="currentColor" /><circle cx="24" cy="16" r="2" fill="currentColor" />',

    circle:   '<circle cx="16" cy="16" r="11" fill="currentColor" />',

    'sign-out':'<path d="M12 5 H5 V27 H12" /><path d="M12 16 H27 M22 11 L27 16 L22 21" />',

    'sign-in': '<path d="M20 5 H27 V27 H20" /><path d="M5 16 H20 M15 11 L20 16 L15 21" />',

    lock:     '<path d="M7 14 H25 V28 H7 Z M10 14 V9 A 6 6 0 0 1 22 9 V14" /><circle cx="16" cy="20" r="1.5" fill="currentColor" />',

    power:    '<path d="M16 4 V16" /><path d="M10 8 A 12 12 0 1 0 22 8" />',

    reboot:   '<path d="M16 4 V16" /><path d="M10 8 A 12 12 0 1 0 22 8" /><path d="M26 9 L22 8 L23 12" />',

    toggle:   '<rect x="4" y="11" width="24" height="10" rx="5" /><circle cx="22" cy="16" r="3" fill="currentColor" />',
    pulse:    '<path d="M2 20 H9 V8 H15 V24 H21 V14 H30" />',

    usb:      '<circle cx="16" cy="27" r="2.5" fill="currentColor" /><path d="M16 27 V6" /><path d="M16 18 L10 12" /><path d="M16 14 L22 10" /><path d="M13 6 L16 3 L19 6 Z" fill="currentColor" /><rect x="8" y="10" width="4" height="4" fill="currentColor" /><circle cx="22" cy="10" r="2" fill="currentColor" />',

    chip:     '<rect x="9" y="9" width="14" height="14" /><rect x="13" y="13" width="6" height="6" /><path d="M9 12 H5 M9 16 H5 M9 20 H5 M23 12 H27 M23 16 H27 M23 20 H27 M12 9 V5 M16 9 V5 M20 9 V5 M12 23 V27 M16 23 V27 M20 23 V27" />',

    camera:   '<path d="M4 10 H10 L12 7 H20 L22 10 H28 V25 H4 Z" /><circle cx="16" cy="17" r="5" /><circle cx="16" cy="17" r="2" fill="currentColor" /><circle cx="24" cy="13" r="1" fill="currentColor" />',

    sliders:  '<path d="M4 9 H10 M14 9 H28 M4 16 H22 M26 16 H28 M4 23 H16 M20 23 H28" /><circle cx="12" cy="9" r="2.5" fill="currentColor" /><circle cx="24" cy="16" r="2.5" fill="currentColor" /><circle cx="18" cy="23" r="2.5" fill="currentColor" />',

    factory:  '<path d="M10 8 A 12 12 0 1 0 22 8" /><path d="M6 9 L10 8 L9 12" /><circle cx="16" cy="17" r="1.6" fill="currentColor" />',
    bullseye: '<circle cx="16" cy="16" r="10" /><circle cx="16" cy="16" r="4" />',

    hdmi:     '<path d="M3 8 H29 V18 L23 24 H9 L3 18 Z" /><path d="M8 15 H24" />',
};

function iconSvg(name, extraStyle) {
    const inner = ICONS[name] || '';
    const style = extraStyle ? ` style="${extraStyle}"` : '';
    return `<svg class="icon" viewBox="0 0 32 32"${style}>${inner}</svg>`;
}

const $  = (s, root) => (root || document).querySelector(s);
const $$ = (s, root) => Array.from((root || document).querySelectorAll(s));

function el(tag, attrs, ...kids) {
    const e = document.createElement(tag);
    if (attrs) for (const k of Object.keys(attrs)) {
        const v = attrs[k];
        if (v === null || v === false || v === undefined) continue;
        if      (k === 'classes' || k === 'class') e.className = v;
        else if (k === 'css')   e.style.cssText += v;
        else if (k === 'style') e.style.cssText += v;
        else if (k === 'html')  e.innerHTML = v;
        else if (k.startsWith('on')) e.addEventListener(k.slice(2).toLowerCase(), v);
        else                    e.setAttribute(k, v);
    }
    for (const kid of kids) {
        if (kid == null || kid === false) continue;
        if (Array.isArray(kid)) {
            for (const k of kid) if (k != null && k !== false) e.appendChild(k.nodeType ? k : document.createTextNode(String(k)));
        } else if (typeof kid === 'object' && kid.nodeType) {
            e.appendChild(kid);
        } else {
            e.appendChild(document.createTextNode(String(kid)));
        }
    }
    return e;
}

function icon(name, css) {
    const wrap = el('span', { class: 'icon-wrap', style: 'display:inline-flex;' });
    wrap.innerHTML = iconSvg(name, css || '');
    return wrap.firstChild;
}

function toast(msg, kind) {
    const t = el('div', { class: 'toast ' + (kind || '') }, msg);
    $('#toasts').appendChild(t);
    setTimeout(() => { t.style.opacity = '0'; t.style.transition = 'opacity 0.5s'; }, 5000);
    setTimeout(() => { t.remove(); }, 5500);
}

function factoryResetAndReboot(msg) {
    toast(msg, 'ok');
    apiPost('/api/factory_reset', {}).catch(e => {
        const m = (e && e.message) || '';
        if (m && !/fetch|network|load failed|abort/i.test(m))
            toast('Factory reset failed: ' + m, 'error');
    });
}

if (sessionStorage.getItem('chc5_auth')) {
    sessionStorage.removeItem('chc5_auth');
}
let authToken = sessionStorage.getItem('chc5_token') || '';
let authUser  = sessionStorage.getItem('chc5_user')  || '';
let authRole  = sessionStorage.getItem('chc5_role')  || '';

function setAuth(token, user, role) {
    authToken = token || '';
    authUser  = user  || '';
    authRole  = role  || '';
    if (authToken) {
        sessionStorage.setItem('chc5_token', authToken);
        sessionStorage.setItem('chc5_user',  authUser);
        sessionStorage.setItem('chc5_role',  authRole);
    } else {
        sessionStorage.removeItem('chc5_token');
        sessionStorage.removeItem('chc5_user');
        sessionStorage.removeItem('chc5_role');
    }
}

async function api(method, path, body, isBinary) {
    const opts = {
        method,
        credentials: 'same-origin',
        headers: {},
    };
    if (authToken) opts.headers['Authorization'] = 'Bearer ' + authToken;
    if (body != null) {
        if (isBinary) {
            opts.headers['Content-Type'] = 'application/octet-stream';
            opts.body = body;
        } else {
            opts.headers['Content-Type'] = 'application/json';
            opts.body = typeof body === 'string' ? body : JSON.stringify(body);
        }
    }
    const r = await fetch(path, opts);
    if (r.status === 401) {
        setAuth('', '', '');
        stopWS();
        showLogin();
        throw new Error('unauthorized');
    }
    let data = null;
    const ct = r.headers.get('content-type') || '';
    if (ct.includes('application/json')) {
        try { data = await r.json(); } catch (_) {}
    } else {
        try { data = await r.text(); } catch (_) {}
    }
    if (!r.ok) {
        const msg = (data && data.error) || ('HTTP ' + r.status);
        const err = new Error(msg);
        err.status = r.status;
        err.data = data;
        throw err;
    }
    return data;
}
const apiGet    = (p)       => api('GET', p);
const apiPost   = (p, body) => api('POST', p, body);

function showLogin() {
    if ($('#login_overlay')) return;

    const appEl = $('#app');
    if (appEl) appEl.style.display = 'none';

    const u = el('input', { class: 'input', type: 'text',
        title: 'Login username (the factory default is "admin").',
        placeholder: '', autocomplete: 'username',
        style: 'width: 100%;' });
    const p = el('input', { class: 'input', type: 'password',
        title: 'Login password.',
        placeholder: '', autocomplete: 'current-password',
        style: 'width: 100%;' });
    const err = el('div', { style: 'color:#c00; font-size:0.85em; min-height:1.2em;' });

    let submitBtn;

    let submitBtn_orig_html = null;
    const setBtnBusy = (text) => {
        if (!submitBtn) return;
        if (submitBtn_orig_html === null) submitBtn_orig_html = submitBtn.innerHTML;
        submitBtn.disabled = true;
        submitBtn.textContent = text;
        submitBtn.style.background = '#999';
        submitBtn.style.cursor = 'not-allowed';
        submitBtn.style.opacity = '0.7';
    };
    const restoreBtn = () => {
        if (!submitBtn) return;
        submitBtn.disabled = false;
        if (submitBtn_orig_html !== null) submitBtn.innerHTML = submitBtn_orig_html;
        submitBtn.style.background = '';
        submitBtn.style.cursor = '';
        submitBtn.style.opacity = '';
    };

    let throttle_countdown_active = false;

    const doLogin = async () => {
        if (submitBtn && submitBtn.disabled) return;

        err.textContent = '';
        authToken = '';

        setBtnBusy('Signing in...');

        try {
            const r = await fetch('/api/login', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({user: u.value, pass: p.value}),
            });
            if (!r.ok) {
                if (r.status === 401 || r.status === 429) {
                    let next_ms = 0;
                    try {
                        const j = await r.json();
                        if (j && j.throttled_ms > 0) {
                            next_ms = Math.min(j.throttled_ms, 60000);
                        }
                    } catch (_) {  }
                    err.textContent = (r.status === 429)
                        ? 'Too many attempts - wait before retrying.'
                        : 'Invalid username or password';
                    if (next_ms >= 7000) {
                        throttle_countdown_active = true;
                        let remaining = Math.ceil(next_ms / 1000);
                        const tick = () => {
                            if (remaining > 0) {
                                setBtnBusy('Login blocked - ' + remaining + 's');
                                remaining--;
                                setTimeout(tick, 1000);
                            } else {
                                throttle_countdown_active = false;
                                restoreBtn();
                            }
                        };
                        tick();
                    } else {
                        restoreBtn();
                    }
                } else {
                    err.textContent = 'Login failed (HTTP ' + r.status + ')';
                    restoreBtn();
                }
                return;
            }
            const d = await r.json();
            if (!d.token) {
                err.textContent = 'Login failed';
                restoreBtn();
                return;
            }
            setAuth(d.token, d.user || u.value, d.role || '');
            setText('tb_user', authUser);
            const ov = $('#login_overlay');
            if (ov) ov.remove();
            if (appEl) appEl.style.display = '';
            navigate();
            startWS();
        } catch (e) {
            err.textContent = 'Login failed: ' + (e.message || e);
            if (!throttle_countdown_active) restoreBtn();
        }
    };
    p.addEventListener('keydown', (e) => { if (e.key === 'Enter') doLogin(); });
    u.addEventListener('keydown', (e) => { if (e.key === 'Enter') p.focus(); });

    const heroPanel = el('div', { class: 'login-hero' },
        el('img', { src: 'circuitvalley.svg', alt: '' }),
        el('div', { style: 'color: #DA291C; font-weight: 800; font-size: 3rem;' }, 'Welcome!'),
        el('div', { style: 'color: #DA291C; font-weight: 400; font-size: 1.25rem;' },
            'CircuitValley CHC5 Camera Configuration'),
    );

    const formInner = el('div', { class: 'login-form-inner' },
        el('div', { style: 'font-size: 2rem; font-weight: 800; margin-bottom: 1rem;' },
            'Sign in'),
        el('div', { class: 'title' }, 'Username'),
        u,
        el('div', { class: 'title' }, 'Password'),
        p,
        el('div', { style: 'color: #aaa; font-size: 90%;' },
            'Sign in with the admin credentials for this unit'),
        err,
        (submitBtn = el('button', {
            class: 'button',
            title: 'Sign in with the username and password above.',
            style: 'margin-top: 1rem;',
            onclick: doLogin,
        }, 'Sign In', icon('sign-in'))),
    );
    const formPanel = el('div', { class: 'login-form' }, formInner);

    const ov = el('div', { id: 'login_overlay', class: 'login-page' },
        el('div', { class: 'login-row' }, heroPanel, formPanel),
    );
    document.body.appendChild(ov);
    setTimeout(() => u.focus(), 0);

    fetch('/api/login_status').then(r => r.ok ? r.json() : null).then(j => {
        if (!j) return;
        if (j.throttled_ms >= 7000 && submitBtn) {
            throttle_countdown_active = true;
            let remaining = Math.ceil(j.throttled_ms / 1000);
            const tick = () => {
                if (remaining > 0) {
                    setBtnBusy('Login blocked - ' + remaining + 's');
                    remaining--;
                    setTimeout(tick, 1000);
                } else {
                    throttle_countdown_active = false;
                    restoreBtn();
                }
            };
            tick();
        }
    }).catch(() => {  });
}

const NAV = [
    { id: 'dashboard',       title: 'Dashboard',            icon: 'desktop'  },
    { id: 'isp',             title: 'ISP Control',          icon: 'toggle'   },
    { id: 'hdmi',            title: 'HDMI Output',          icon: 'hdmi'     },
    { id: 'iosync',          title: 'I/O & Sync',           icon: 'pulse'    },
    { id: 'ptp',             title: 'Time Sync (PTP)',      icon: 'clock'    },
    { id: 'sensor',          title: 'Sensor Library',       icon: 'camera'   },
    { id: 'bitstream',       title: 'Bitstream Library',    icon: 'chip'     },
    { id: 'usb_fw',          title: 'USB Firmware Library', icon: 'usb'      },
    { id: 'firmware_update', title: 'System Firmware Update', icon: 'import'   },
    { id: 'network',         title: 'Device Settings',      icon: 'sliders'  },
    { id: 'log',             title: 'System Log',           icon: 'bell'     },
    { id: 'admin_password',  title: 'Change Password',      icon: 'lock'     },
];

function buildShell() {
    const app = $('#app');
    app.innerHTML = '';
    app.className = 'shell';

    const toolbar = el('div', { class: 'top-toolbar' });

    const hamburger = el('button', {
        class: 'hamburger',
        title: 'Toggle sidebar',
        onclick: () => { app.classList.toggle('sidebar-collapsed'); },
    }, el('span', { html: '&#9776;' }));
    toolbar.appendChild(hamburger);

    const brand = el('div', { class: 'brand' },
        el('img', { class: 'brand-logo', src: 'circuitvalley.svg', alt: 'CircuitValley' }),
        el('div', { class: 'brand-subtitle' }, 'CHC5 Camera Configuration'),
    );
    toolbar.appendChild(brand);

    toolbar.appendChild(el('div', { id: 'tb_title', style: 'display:none;' }, ''));

    const right = el('div', { class: 'toolbar-right' },
        el('span', { class: 'toolbar-product', id: 'tb_serial',
                     title: 'Device serial number (from the on-board identity EEPROM)' }, ''),
        el('span', { class: 'toolbar-sep' }, '|'),
        el('span', { class: 'toolbar-product', id: 'tb_sensor',
                     title: 'Sensor archive currently ACTIVE (the one the pipeline is '
                            + 'running, not the one staged for next boot)' }, ''),
        el('span', { class: 'toolbar-sep' }, '|'),
        el('span', { class: 'toolbar-product' }, 'CircuitValley CHC5'),
        el('span', { class: 'toolbar-sep' }, '|'),
        el('span', { class: 'toolbar-username', id: 'tb_user' }, ''),
        el('span', { class: 'toolbar-user-icon', html: iconSvg('user') }),
        el('button', {
            class: 'logout-btn',
            title: 'Log out',
            onclick: async () => {
                try { await apiPost('/api/logout'); } catch (_) {}
                setAuth('', '', '');
                stopWS();
                showLogin();
            },
        }, el('span', { html: iconSvg('sign-out') })),
    );
    toolbar.appendChild(right);
    app.appendChild(toolbar);

    const body = el('div', { class: 'shell-body' });

    const sidebar = el('div', { class: 'sidebar-wrapper' });
    const list = el('div', { class: 'sidebar-item-list' });
    for (const n of NAV) {
        const item = el('div', {
            class: 'sidebar-item',
            id: 'nav_' + n.id,
            onclick: () => { location.hash = '#' + n.id; },
        }, icon(n.icon, n.iconCss), el('span', { class: 'sidebar-item-label' }, n.title));
        list.appendChild(item);
    }
    sidebar.appendChild(list);
    body.appendChild(sidebar);

    const main = el('div', { class: 'page-wrapper' });
    main.appendChild(el('div', {
        id: 'page',
        class: 'page',
        style: 'flex:0 0 auto; padding:0.75rem; gap:0.5rem; min-height:2rem; display:flex; flex-direction:column; flex-grow:1; overflow:auto; width:100%;',
    }));
    body.appendChild(main);

    app.appendChild(body);

    if (authUser) setText('tb_user', authUser);

    apiGet('/api/device_info')
        .then(di => setText('tb_serial',
                            di && di.provisioned && di.serial ? di.serial
                                                              : '(not provisioned)'))
        .catch(() => {});

    apiGet('/api/sensor_status')
        .then(st => setText('tb_sensor',
                            st && st.currently_active ? st.currently_active
                                                      : '(no sensor)'))
        .catch(() => {});
}

function panel(opts, ...kids) {
    const css = (opts && opts.css) || '';
    return el('div', { class: 'panel ' + ((opts && opts.extraClass) || ''), style: css }, ...kids);
}

function panelTitle(text) {
    return el('div', { class: 'title' }, text);
}

function helpPanel(help) {
    const kids = [ panelTitle('Help & Tips') ];
    if (help.intro)
        kids.push(el('div', { style: 'margin:0 0 0.7rem; color:#555;' }, help.intro));
    if (help.controls && help.controls.length) {
        kids.push(el('div', { style: 'font-weight:600; margin:0.3rem 0 0.35rem;' }, 'Controls'));
        const grid = el('div', { style: 'display:grid; grid-template-columns:max-content 1fr; '
            + 'gap:0.3rem 1.1rem; align-items:baseline; margin-bottom:0.2rem;' });
        for (const c of help.controls) {
            grid.appendChild(el('div', { style: 'font-weight:600; white-space:nowrap;' }, c.name));
            grid.appendChild(el('div', { style: 'color:#444;' }, c.desc));
        }
        kids.push(grid);
    }
    if (help.tips && help.tips.length) {
        const ul = el('ul', { style: 'margin:'
            + (help.controls && help.controls.length ? '0.7rem' : '0.2rem')
            + ' 0 0; padding-left:1.2rem; color:#444;' });
        for (const t of help.tips) ul.appendChild(el('li', { style: 'margin:0.2rem 0;' }, t));
        kids.push(ul);
    }
    return panel({ css: 'width:100%; margin-top:0.4rem;' }, ...kids);
}

function libraryTitle(text) {
    return el('div', {
        class: 'title',
        style: 'display:flex; justify-content:space-between; align-items:baseline;',
    },
        el('span', {}, text),
        el('span', {
            class: 'upload_storage',
            style: 'font-size: 0.85em; font-family: monospace; color: #555; font-weight: normal;',
        }, 'Free: ...'),
    );
}

function labeled(labelText, valueNode, opts) {
    return el('div', { class: 'labeled', style: (opts && opts.css) || '',
                       title: (opts && opts.title) || null },
        el('div', { class: 'label', title: (opts && opts.title) || null }, labelText),
        valueNode,
    );
}

function labeledTwo(aLabel, aValue, bLabel, bValue, aTitle, bTitle) {
    return el('div', {
        style: 'display: grid; grid-template-columns: 1fr 1fr; gap: 0 2rem;'
    },
        labeled(aLabel, aValue, aTitle ? { title: aTitle } : undefined),
        labeled(bLabel, bValue, bTitle ? { title: bTitle } : undefined),
    );
}

function actionButton(label, iconName, onClick, opts) {
    return el('button', {
        class: 'button',
        title: (opts && opts.title) || label,
        disabled: (opts && opts.disabled) ? '' : null,
        style: (opts && opts.css) || '',
        onclick: onClick,
    }, icon(iconName), label);
}

async function saveImaging(btn, body, okMsg) {
    btn.disabled = true;
    try {
        await apiPost('/api/imaging', body);
        toast(okMsg || 'Settings saved', 'ok');
    } catch (e) {
        toast('Save failed: ' + e.message, 'error');
    } finally {
        btn.disabled = false;
    }
}

function sliderFor(num, min, max, step) {
    num.style.width = '4.5rem';
    const rng = el('input', { type: 'range', class: 'slider', min: String(min),
        max: String(max), step: String(step || 1), style: 'flex: 1; min-width: 6rem;' });
    num.addEventListener('input', () => { rng.value = num.value; });
    rng.addEventListener('input', () => {
        num.value = rng.value;
        num.dispatchEvent(new Event('input'));
    });
    return el('div', { style: 'display: flex; align-items: center; gap: 0.6rem; width: 15rem;' },
        rng, num);
}

async function deleteEntry(btn, apiPath, name) {
    const wasPoll = !!(activePage && activePage.refreshMs);
    stopPolling();

    const origHTML = btn ? btn.innerHTML : null;
    if (btn) {
        btn.disabled = true;
        btn.innerHTML = '';
        btn.appendChild(icon('ellipsis-horizontal'));
        btn.appendChild(document.createTextNode(' Deleting...'));
    }
    toast("Deleting '" + name + "' - please wait...");

    try {
        await apiPost(apiPath, { name: name, force: false });
        toast("Deleted '" + name + "'", 'ok');
    } catch (e) {
        toast('Delete failed: ' + ((e && e.message) || e), 'error');
    } finally {
        if (btn) { btn.disabled = false; btn.innerHTML = origHTML; }
        if (activePage && typeof activePage.refresh === 'function') {
            try { activePage.refresh(); } catch (_) {  }
        }
        if (wasPoll && activePage) startPolling(activePage);
    }
}

function valueSpan(id, text, css) {
    return el('div', { id, style: css || '' }, text == null ? '' : String(text));
}

function kvRow(id, label, title) {
    return [
        el('div', { class: 'kvk', title: title || null }, label),
        el('div', { class: 'kvv', id: id, title: title || null }, '-'),
    ];
}

function kvShow(id, text) {
    const v = document.getElementById(id);
    if (!v) return;
    const empty = (text == null || text === '');
    v.textContent = empty ? '' : String(text);
    const disp = empty ? 'none' : '';
    v.style.display = disp;
    if (v.previousElementSibling) v.previousElementSibling.style.display = disp;
}

function setText(id, text) {
    const e = document.getElementById(id);
    if (e) e.textContent = text == null ? '' : String(text);
}

function unlistedText(n) {
    return n ? (n + (n === 1 ? ' bad archive' : ' bad archives') + ' unlisted') : '';
}

function esc(s) {
    return String(s == null ? '' : s)
        .split('&').join('&amp;')
        .split('<').join('&lt;')
        .split('>').join('&gt;')
        .split('"').join('&quot;');
}

function setHtml(id, html) {
    const e = document.getElementById(id);
    if (e) e.innerHTML = html;
}

const fmt = {
    none: (v) => (v == null || v === '') ? '(none)' : v,
    int:  (v) => v == null ? '0' : String(v),
    pct:  (v) => (v == null ? '0' : String(v)) + '%',
    bs:   (v) => v == null ? '' : String(v),
};

function humanSize(n) {
    if (n == null || isNaN(n)) return '';
    n = Number(n);
    if (n < 1024)               return n + ' B';
    if (n < 1024 * 1024)        return (n / 1024).toFixed(1) + ' K';
    if (n < 1024 * 1024 * 1024) return (n / (1024 * 1024)).toFixed(1) + ' M';
    return (n / (1024 * 1024 * 1024)).toFixed(1) + ' G';
}

function truncDesc(s, n) {
    if (!s) return '';
    if (n == null) n = 64;
    s = String(s);
    if (s.length <= n) return s;
    return s.slice(0, n - 1) + '...';
}

let pollTimer = null;
let activePage = null;

function pageInterval(page) {
    if (!page) return 0;
    const ms = (typeof page.refreshMs === 'function') ? page.refreshMs()
                                                      : page.refreshMs;
    return (typeof ms === 'number' && ms > 0) ? ms : 0;
}

function startPolling(page) {
    stopPolling();
    if (!page || typeof page.refresh !== 'function' || !pageInterval(page)) return;

    const tick = async () => {
        try { await page.refresh(); } catch (e) {  }
        if (pollTimer === null || activePage !== page) return;
        const ms = pageInterval(page);
        pollTimer = ms ? setTimeout(tick, ms) : null;
    };
    pollTimer = setTimeout(tick, pageInterval(page));
}
function stopPolling() {
    if (pollTimer) { clearTimeout(pollTimer); pollTimer = null; }
}

let wsConn = null;
let wsReconnectTimer = null;

function stopWS() {
    if (wsReconnectTimer) { clearTimeout(wsReconnectTimer); wsReconnectTimer = null; }
    if (wsConn) {
        try { wsConn.close(); } catch (_) {}
        wsConn = null;
    }
}

function startWS() {
    stopWS();
    if (!authToken) return;
    const proto = (location.protocol === 'https:') ? 'wss://' : 'ws://';
    const url   = proto + location.host + '/ws';
    const subprotos = ['chc5-state', 'bearer.' + authToken];

    try {
        wsConn = new WebSocket(url, subprotos);
    } catch (_) {
        wsReconnectTimer = setTimeout(startWS, 5000);
        return;
    }
    wsConn.onmessage = () => {
        try {
            if (activePage && typeof activePage.refresh === 'function') {
                activePage.refresh();
            }
        } catch (_) {  }
    };
    wsConn.onclose = () => {
        wsConn = null;
        if (wsReconnectTimer) clearTimeout(wsReconnectTimer);
        wsReconnectTimer = setTimeout(startWS, 5000);
    };
    wsConn.onerror = () => {
    };
}

async function uploadFile(file, store, btn) {
    const CHUNK = 64 * 1024;
    const wasPoll = pollTimer;
    stopPolling();

    const origHTML = btn ? btn.innerHTML : null;
    const setBusy = (text) => {
        if (!btn) return;
        btn.disabled = true;
        btn.innerHTML = '';
        btn.appendChild(icon('ellipsis-horizontal'));
        btn.appendChild(document.createTextNode(' ' + text));
    };
    const restoreBtn = () => {
        if (!btn) return;
        btn.disabled = false;
        btn.innerHTML = origHTML;
    };

    try {
        setBusy('preparing...');
        toast('Upload started: ' + file.name + ' (' + humanSize(file.size) + ')');
        const beg = await apiPost('/api/ota/begin', {
            store, filename: file.name, size: file.size,
        });
        const handle = beg.handle;
        let sent = 0;
        while (sent < file.size) {
            const slice = file.slice(sent, sent + CHUNK);
            const buf = await slice.arrayBuffer();
            const r = await fetch(
                '/api/ota/write?h=' + encodeURIComponent(handle) + '&o=' + sent,
                { method: 'POST', body: buf, credentials: 'same-origin',
                  headers: { 'Authorization': 'Bearer ' + authToken } });
            if (!r.ok) {
                let detail = '';
                try { const j = await r.json(); detail = j.error || ''; } catch (_) {}
                throw new Error('chunk failed at ' + sent + (detail ? ': ' + detail : ''));
            }
            sent += slice.size;
            setBusy('uploading ' + Math.round(100 * sent / file.size) + '%');
        }
        setBusy('processing...');
        toast('Processing on Camera (Validating...)');
        await apiPost('/api/ota/end', { handle });
        toast('Upload complete: ' + file.name);
    } catch (e) {
        toast('Upload failed: ' + e.message, 'error');
    } finally {
        restoreBtn();
        if (wasPoll && activePage) startPolling(activePage);
    }
}

async function refreshStorageBadge() {
    try {
        const s = await apiGet('/api/storage');
        const txt = (s && s.ok)
            ? 'Free: ' + humanSize(s.free) + ' / ' + humanSize(s.total)
            : 'Free: ?';
        document.querySelectorAll('.upload_storage').forEach(e => {
            e.textContent = txt;
        });
    } catch (_) {  }
}

function uploadWidget(opts) {
    let btn;
    const fi = el('input', {
        type: 'file',
        accept: opts.accept || '',
        style: 'display:none;',
        onchange: async (e) => {
            const f = e.target.files && e.target.files[0];
            e.target.value = '';
            if (f) {
                await uploadFile(f, opts.store, btn);
                refreshStorageBadge();
                if (activePage && activePage.refresh) activePage.refresh();
            }
        },
    });
    btn = el('button', { class: 'button',
        title: opts.title || 'Choose a file to upload from your computer.',
        onclick: () => fi.click() },
        icon('ellipsis-horizontal'),
        opts.title || 'select file');

    setTimeout(refreshStorageBadge, 0);

    return el('div', { class: 'panel',
            style: 'margin-top: 0.5rem;' + (opts.width ? ' width: ' + opts.width + ';' : '') },
        panelTitle(opts.titleBar),
        el('div', { class: 'labeled', style: 'margin-top: auto;' },
            el('div', { class: 'label' }, opts.label || ''),
            btn,
        ),
        fi,
        el('div', { style: 'color: #888; font-size: 0.85em; margin-top: 0.3rem; '
            + 'white-space: normal; word-break: break-word;' }, opts.helper || ''),
    );
}

const pageDashboard = {
    refreshMs: 0,
    async render() {
        const p = $('#page');
        p.innerHTML = '';

        const sysInfo = panel({ css: 'width: fit-content; min-width: 33rem;', extraClass: 'kvfit' },
            panelTitle('System Info'),
            el('div', { class: 'kvgrid' },
                kvRow('d_v_boot_env', 'Boot platform:', 'Which boot slot/platform is running (e.g. SD or QSPI, A/B slot)'),
                kvRow('d_v_chc5',     'CHC5 System:',   'CHC5 system release version (the whole-image build stamp)'),
                kvRow('d_v_flavor',   'Image flavour:', 'dev: the update keyring trusts official releases AND bundles signed with the open dev key from the source repo. production: official releases only. debug: the open dev key only.'),
                kvRow('d_v_bootimg',  'Boot image copy:', 'Which BOOT.BIN copy the boot ROM used. primary = the working copy at flash offset 0. golden = the working copy is damaged and the camera is running on the spare copy at 2 MB: re-flash the boot layer soon, there is no second spare.'),
                kvRow('d_v_pd',       'Platformd:',     'chc5_platformd daemon version (privileged config owner)'),
                kvRow('d_v_camcfg',   'Camcfgd:',       'camcfgd daemon version (V4L2 pipeline + AE)'),
                kvRow('d_v_gvcp',     'GVCP:',          'GigE Vision (GVCP) server version'),
                kvRow('d_v_kernel',   'Kernel:',        'Running Linux kernel version'),
                kvRow('d_v_bs',       'Bitstream:',     'FPGA bitstream currently loaded'),
                kvRow('d_v_webd',     'webd:',          'chc5_webd version (this web UI server)'),
                kvRow('d_v_usb',      'usb:',           'USB3-Vision controller firmware version'),
                kvRow('d_v_time',     'Device time:',   'Current device clock (UTC)'),
            ),
        );

        const deviceInfo = panel({ css: 'min-width: 22rem;' },
            panelTitle('Device'),
            el('div', { class: 'kvgrid' },
                kvRow('d_dev_serial',  'Serial:',     'Device serial number (from the on-board identity EEPROM)'),
                kvRow('d_dev_product', 'Product:',    'Product name/model (from the EEPROM)'),
                kvRow('d_dev_hw',      'HW version:', 'Hardware revision (from the EEPROM)'),
                kvRow('d_dev_mac',     'MAC:',        'Ethernet MAC address'),
                kvRow('d_dev_batch',   'Batch:',      'Manufacturing batch/lot (from the EEPROM)'),
                kvRow('d_dev_mfg',     'Mfg date:',   'Manufacturing date (from the EEPROM)'),
            ),
        );

        const cfg = panel({ css: 'min-width: 24rem;' },
            panelTitle('Current Configuration'),
            labeled('USB firmware',       valueSpan('d_at_usb',    '-'),
                    { title: 'Active USB3-Vision controller firmware entry' }),
            labeled('FPGA bitstream',     valueSpan('d_at_bs',     '-'),
                    { title: 'Active FPGA bitstream entry (loaded now)' }),
            labeled('Sensor (active)',    valueSpan('d_at_sn_act', '-'),
                    { title: 'Sensor package currently running' }),
            labeled('Sensor (next boot)', valueSpan('d_at_sn_nxt', '-'),
                    { title: 'Sensor package staged for the next reboot (sensor changes need a reboot)' }),
            el('div', { id: 'd_reboot_banner' }),
        );

        const idToggle = el('input', { type: 'checkbox', class: 'toggle', id: 'd_identify',
            title: 'Strobe the status LED so you can find this camera' });
        idToggle.addEventListener('change', () => {
            apiPost('/api/identify', { enable: idToggle.checked ? 1 : 0 })
                .then(() => toast(idToggle.checked ? 'Identify on' : 'Identify off'))
                .catch(e => { toast(e.message, 'error'); idToggle.checked = !idToggle.checked; });
        });
        apiGet('/api/identify').then(j => { idToggle.checked = !!(j && j.enable); }).catch(() => {});
        const locatePanel = panel({ css: 'width: 26rem;' },
            panelTitle('Locate'),
            el('label', { style: 'display:inline-flex;align-items:center;gap:0.6rem;cursor:pointer;' },
                idToggle,
                el('span', { class: 'label' }, 'Identify - strobe the status LED to find this camera')),
        );

        const storagePanel = panel({ css: 'min-width: 22rem;' },
            panelTitle('Storage Health'),
            el('div', { class: 'kvgrid' },
                kvRow('d_st_wear', 'Flash wear (QSPI):',       'Estimated QSPI flash wear: peak erase count vs the ~100,000 P/E-cycle rating'),
                kvRow('d_st_ec',   'Peak erase count:',        'Highest erase count across all QSPI flash blocks (UBI max erase count)'),
                kvRow('d_st_bad',  'Bad blocks:',              'Number of bad/retired flash blocks (rises near end of flash life)'),
                kvRow('d_st_sd',   'SD written (since boot):', 'Bytes written to the SD card since boot (from kernel diskstats)'),
                kvRow('d_st_free', 'Data partition free:',     'Free space on the /var data partition'),
            ),
        );

        p.appendChild(el('div', {
            class: 'container',
            style: 'gap: 0.5rem; align-items: flex-start; flex-wrap: wrap;',
        }, sysInfo, deviceInfo, storagePanel, cfg, locatePanel));

        apiGet('/api/storage').then(s => {
            const w = s.wear || {};
            if (w.ubi_present) {
                const pm = w.wear_permille || 0;
                kvShow('d_st_wear', (pm / 10).toFixed(1) + '%'
                    + ' (' + (w.max_ec || 0) + ' / ' + (w.rated_cycles || 100000) + ' cycles)');
                kvShow('d_st_ec',  w.max_ec || 0);
                kvShow('d_st_bad', (w.bad_peb_count || 0)
                    + ' (reserve ' + (w.reserved_for_bad || 0)
                    + ' / ' + (w.total_eraseblocks || 0) + ' total)');
            } else {
                kvShow('d_st_wear', 'n/a (not a QSPI boot)');
                kvShow('d_st_ec',  '');
                kvShow('d_st_bad', '');
            }
            if (w.sd_present) {
                kvShow('d_st_sd', humanSize(w.sd_written_bytes || 0));
            } else {
                kvShow('d_st_sd', 'n/a (not an SD boot)');
            }
            kvShow('d_st_free', s.ok
                ? humanSize(s.free) + ' / ' + humanSize(s.total)
                : '-');
        }).catch(() => {});

        await this.refresh();
    },

    async refresh() {
        let d = {};
        try { d = await apiGet('/api/dashboard'); }
        catch (e) { return; }
        const ver = d.versions || {};
        const at  = d.active_triplet || {};

        setText('d_v_chc5',   ver.chc5      || '-');
        setText('d_v_flavor', ver.flavor === 'dev'  ? 'DEV \u2014 accepts community-signed updates'
                            : ver.flavor === 'prod' ? 'production \u2014 official releases only'
                            : ver.flavor === 'debug' ? 'DEBUG \u2014 dev-key updates only'
                            : (ver.flavor || '-'));
        setText('d_v_bootimg', !ver.boot_image ? '-'
                             : ver.boot_image.indexOf('golden') === 0 ? '\u26a0 GOLDEN copy in use \u2014 working BOOT.BIN damaged, re-flash the boot layer (' + ver.boot_image + ')'
                             : ver.boot_image);
        setText('d_v_pd',     ver.platformd || '-');
        setText('d_v_camcfg', ver.camcfgd   || '-');
        setText('d_v_gvcp',   ver.gvcp      || '-');
        setText('d_v_kernel', ver.kernel    || '-');
        setText('d_v_bs',     ver.bitstream || '-');
        setText('d_v_webd',   ver.webd      || '-');
        setText('d_v_usb',    ver.usb       || '-');
        setText('d_v_boot_env', (ver.boot_env || '-')
            + (ver.boot_env_gen ? ' [gen ' + ver.boot_env_gen + ']' : ''));

        try {
            const t = await apiGet('/api/system/time');
            setText('d_v_time', t.iso || '-');
        } catch (_) {}

        try {
            const di = await apiGet('/api/device_info');
            const dv = v => (v ? v : '-');
            setText('d_dev_serial',  di && di.provisioned ? dv(di.serial) : '(not provisioned)');
            setText('d_dev_product', dv(di && di.product));
            setText('d_dev_hw',      dv(di && di.hw_version));
            setText('d_dev_mac',     dv(di && di.mac));
            setText('d_dev_batch',   di && di.batch ? di.batch : '-');
            setText('d_dev_mfg',     dv(di && di.mfg_date));
        } catch (_) {}

        setText('d_at_usb',    fmt.none(at.usb_fw));
        setText('d_at_bs',     fmt.none(at.bitstream));
        setText('d_at_sn_act', fmt.none(at.sensor_active || at.sensor));
        setText('d_at_sn_nxt', fmt.none(at.sensor_installed || d.sensor_next_boot));

        const banner = document.getElementById('d_reboot_banner');
        if (banner) {
            const box = (bg, bd, fg, html) =>
                "<div style='background:" + bg + ";border:1px solid " + bd
                + ";color:" + fg + ";padding:0.4rem 0.6rem;border-radius:4px;"
                + "margin-top:0.5rem;font-size:0.9em;'>&#9888; " + html + "</div>";
            const errBox  = h => box('#f8d7da', '#f5c6cb', '#721c24', h);
            const warnBox = h => box('#fff3cd', '#ffeeba', '#856404', h);

            const active    = at.sensor_active || at.sensor || '';
            const installed = at.sensor_installed || d.sensor_next_boot || '';
            const out = [];

            if (d.factory_button_stuck) {
                out.push(errBox('The <b>factory-reset button appears stuck</b> (held '
                    + 'at boot) - the camera ignored it and booted normally. '
                    + 'Check the button and its wiring.'));
            }

            if (d.var_degraded) {
                out.push(errBox('<b>Storage failed</b> (' + esc(d.var_degraded) + ') - the '
                    + 'camera is running from RAM, and settings and uploads are lost at '
                    + 'the next reboot. A factory reset re-creates the storage.'));
            }

            const fr = d.factory_reset;
            if (fr && fr.result === 'pending') {
                out.push(warnBox('A <b>factory reset is not finished</b> - it completes at '
                    + 'the next boot.'));
            } else if (fr && fr.result === 'failed') {
                out.push(errBox('The last <b>factory reset did not complete</b>'
                    + (fr.error ? ': ' + esc(fr.error) : '') + '. Details on Network &gt; '
                    + 'Device Actions.'));
            }

            if (installed && !active) {
                out.push(errBox('FPGA bitstream / sensor <b>failed to load</b> at boot. '
                    + 'A reboot will not fix a bad bitstream, sensor or wiring - '
                    + 'check the Logs page.'));
            } else if (installed && active && installed !== active) {
                out.push(warnBox('Sensor change staged - reboot to apply.'));
            }

            banner.innerHTML = out.join('');
        }
    },
};

const pageIsp = {
    help: {
        tips: [
            'Exposure and Gain can be controlled by different sources depending on the mode: AE on -> AE controls them; a USB/GigE host streaming with AE off -> the host; no host + Standalone HDMI + AE off -> the HDMI fps sets exposure (~1/fps, with no separate exposure control in standalone).',
            'Picking metering by scene: Spot for a small subject against a very different background; Histogram for high-contrast (bright window + dark room); ETTR only for capture/SNR (it deliberately looks bright). Center-weighted otherwise.',
            'Target brightness has no effect in ETTR metering \u2014 ETTR drives its own fixed near-clip target.',
            'The HDMI output (enable, standalone, fps, crop) lives on the HDMI Output tab.',
        ],
    },
    refreshMs: 0,
    async render() {
        const p = $('#page');
        p.innerHTML = '';

        const aeTarget = el('input', { class: 'input', id: 'im_ae', type: 'number',
            title: 'Auto-exposure target brightness, 0-100% (mid-grey is ~50%).',
            min: '0', max: '100', step: '1', style: 'width: 8rem;' });
        const meter    = el('select', { class: 'input', id: 'im_meter', style: 'width: 12rem;',
            title: 'AE metering: how the scene is weighted when measuring brightness. '
                 + 'ETTR maximizes SNR by exposing to the right (brightest pixels just below '
                 + 'clipping) - the image looks brighter and the Target slider does not apply.' },
            el('option', { value: '0' }, 'Average'),
            el('option', { value: '1' }, 'Center-weighted'),
            el('option', { value: '2' }, 'Spot'),
            el('option', { value: '3' }, 'Histogram'),
            el('option', { value: '4' }, 'ETTR (max SNR)'),
            el('option', { value: '5' }, 'ROI (region)'));
        const aeSpeed  = el('input', { class: 'input', id: 'im_ae_speed', type: 'number',
            title: 'AE responsiveness, 1-100. Higher converges faster (may step more); '
                 + 'lower is slower and smoother.',
            min: '1', max: '100', step: '1', style: 'width: 8rem;' });
        const aePrio   = el('select', { class: 'input', id: 'im_ae_prio', style: 'width: 12rem;',
            title: 'Which axis AE adjusts first: Exposure-first (less noise) or '
                 + 'Gain-first (keeps the shutter short for motion).' },
            el('option', { value: '0' }, 'Exposure first'),
            el('option', { value: '1' }, 'Gain first'));
        const aeHi     = el('select', { class: 'input', id: 'im_ae_hi', style: 'width: 12rem;',
            title: 'Highlight protection: darken to recover blown-out highlights '
                 + '(uses the histogram).' },
            el('option', { value: '0' }, 'Off'),
            el('option', { value: '1' }, 'Normal'),
            el('option', { value: '2' }, 'Strong'));
        const aeFlick  = el('select', { class: 'input', id: 'im_ae_flick', style: 'width: 12rem;',
            title: 'Anti-flicker: lock exposure to whole mains-light cycles to stop '
                 + 'banding/flicker under AC lighting. Pick your region\u2019s mains '
                 + 'frequency. May overexpose very bright scenes (min 8-10 ms exposure).' },
            el('option', { value: '0' }, 'Off'),
            el('option', { value: '1' }, '50 Hz'),
            el('option', { value: '2' }, '60 Hz'));
        const forceAe  = el('input', { type: 'checkbox', class: 'toggle', id: 'im_force_ae',
            title: 'Development / debugging aid, and the way to run auto-exposure for '
                 + 'Standalone HDMI where no host is connected. Turns AE (exposure + '
                 + 'gain) on from the web. It acts WHEN YOU MOVE IT - it is not '
                 + 're-applied when a stream starts, and it is not a lock: a USB/GigE '
                 + 'host\'s ExposureAuto/GainAuto is absolute and takes the axis back '
                 + 'the moment it is written. To re-assert after a host has changed it, '
                 + 'toggle off then on. Unticking hands AE back to whatever the host '
                 + 'last asked for, not simply "off". Persisted across reboot. The '
                 + 'read-only "Auto Exposure" field above always shows what is actually '
                 + 'running.' });
        const forceAwb = el('input', { type: 'checkbox', class: 'toggle', id: 'im_force_awb',
            title: 'Development / debugging aid, and the way to run auto-white-balance '
                 + 'for Standalone HDMI where no host is connected. Turns AWB '
                 + '(BalanceWhiteAuto) on from the web, independently of auto-exposure/'
                 + 'gain. It acts WHEN YOU MOVE IT - it is not re-applied when a stream '
                 + 'starts, and it is not a lock: a USB/GigE host\'s BalanceWhiteAuto is '
                 + 'absolute and takes the axis back the moment it is written. To '
                 + 're-assert after a host has changed it, toggle off then on. Unticking '
                 + 'turns AWB off; choosing a colour temperature other than Auto also '
                 + 'turns it off and applies that preset\'s manual gains. Persisted '
                 + 'across reboot. The read-only "Auto White Balance" field above always '
                 + 'shows what is actually running.' });

        const aeSave = el('button', { class: 'button',
            title: 'Save the Auto Exposure settings (applies within ~1-2 s).',
            onclick: () => {
                const pct = Math.max(0, Math.min(100, parseInt(aeTarget.value, 10) || 0));
                saveImaging(aeSave, {
                    ae_force_enable: forceAe.checked ? 1 : 0,
                    ae_target_permille: pct * 10,
                    ae_metering_mode: Math.max(0, Math.min(5, parseInt(meter.value, 10) || 0)),
                    ae_speed: Math.max(1, Math.min(100, parseInt(aeSpeed.value, 10) || 40)),
                    ae_priority: Math.max(0, Math.min(1, parseInt(aePrio.value, 10) || 0)),
                    ae_highlight: Math.max(0, Math.min(2, parseInt(aeHi.value, 10) || 0)),
                    ae_flicker: Math.max(0, Math.min(2, parseInt(aeFlick.value, 10) || 0)),
                });
            },
        }, icon('save'), 'Save');

        const aeReset = el('button', { class: 'button',
            title: 'Reset the Auto Exposure settings to the default (sensor archive '
                 + 'value if provided, else the built-in default). Leaves Force-AE '
                 + 'and Run-Once untouched.',
            onclick: () => {
                aeTarget.value = 35;  aeSpeed.value = 75;
                meter.value = '1';  aePrio.value = '0';  aeHi.value = '1';  aeFlick.value = '0';
                [aeTarget, aeSpeed].forEach(n => n.dispatchEvent(new Event('input')));
                saveImaging(aeReset, {
                    ae_target_permille: 350,
                    ae_metering_mode:   1,
                    ae_speed:           75,
                    ae_priority:        0,
                    ae_highlight:       1,
                    ae_flicker:         0,
                }, 'AE settings reset to default');
            },
        }, icon('factory'), 'Reset to default');

        const awbNum = (id, step, title) => el('input', { type: 'number', class: 'input',
            id, step: String(step), min: '0', placeholder: 'default', title,
            style: 'width: 7rem;' });
        const awbMin  = awbNum('im_awb_min', 0.05,
            'Lowest per-channel gain auto-WB may apply (range 0.05-1.0). '
          + 'Leave empty to use the per-sensor default.');
        const awbMax  = awbNum('im_awb_max', 0.1,
            'Highest per-channel gain auto-WB may apply (range 1.0-16.0). '
          + 'Leave empty to use the per-sensor default.');
        let bakedRate = 15;
        const awbRate = el('input', { class: 'input', id: 'im_awb_rate', type: 'number',
            title: 'Auto-WB convergence speed, 1-100. Higher = faster but noisier; '
                 + 'lower = smoother/slower. Defaults to the per-sensor baked rate.',
            min: '1', max: '100', step: '1', style: 'width: 8rem;' });
        const awbCt = el('select', { class: 'input', id: 'im_awb_ct', style: 'width: 12rem;',
            title: 'Pick the illuminant by colour temperature. A preset turns auto-WB off '
                 + 'and applies the per-sensor calibrated gains for that Kelvin; "Auto '
                 + '(Dynamic)" tracks the scene continuously (gray-world AWB). Entries '
                 + 'marked "No CAL" have no gains in this sensor\'s archive, so they only '
                 + 'switch auto-WB off and leave the white balance neutral.' },
            el('option', { value: '0' },  'Auto (Dynamic)'),
            el('option', { value: '32' }, '3200 K (tungsten)'),
            el('option', { value: '48' }, '4800 K'),
            el('option', { value: '56' }, '5600 K (daylight)'),
            el('option', { value: '65' }, '6500 K (cool)'));
        const awbSave = el('button', { class: 'button',
            title: 'Save the AWB settings (applies within ~1-2 s). Blank tuning fields = '
                 + 'use the per-sensor default value.',
            onclick: () => {
                const f = (e, scale, hi) => {
                    const x = parseFloat(e.value);
                    if (!isFinite(x) || x <= 0) return 0;
                    return Math.max(0, Math.min(hi, Math.round(x * scale)));
                };
                saveImaging(awbSave, {
                    awb_force_enable: forceAwb.checked ? 1 : 0,
                    awb_color_temp_d100: parseInt(awbCt.value, 10) || 0,
                    awb_gain_min_x100: f(awbMin, 100, 100),
                    awb_gain_max_x10:  f(awbMax, 10, 160),
                    awb_rate_x100:     Math.max(1, Math.min(100, parseInt(awbRate.value, 10) || 15)),
                }, 'AWB settings saved');
            },
        }, icon('save'), 'Save');

        const awbReset = el('button', { class: 'button',
            title: 'Clear the AWB min/max gain + rate overrides and go back to the '
                 + 'per-sensor default values.',
            onclick: () => {
                awbMin.value = '';  awbMax.value = '';
                awbRate.value = bakedRate;  awbRate.dispatchEvent(new Event('input'));
                saveImaging(awbReset, {
                    awb_gain_min_x100: 0,
                    awb_gain_max_x10:  0,
                    awb_rate_x100:     0,
                }, 'AWB tuning reset to default');
            },
        }, icon('factory'), 'Reset to default');

        let aeOnceSeq = 0;
        const aeOnceBtn = el('button', { class: 'button',
            title: 'Run auto-exposure once, then hold the result. Standalone only - it is ' +
                   'ignored while a USB/GigE host is streaming (the host controls AE then) ' +
                   'and needs an active stream. Press again to re-run.',
            onclick: async () => {
                let cur = aeOnceSeq;
                try {
                    const d = await apiGet('/api/imaging');
                    if (d && typeof d.ae_once === 'number') cur = d.ae_once;
                } catch (e) {  }
                aeOnceSeq = (cur + 1) & 0xFF;
                forceAe.checked = false;
                saveImaging(aeOnceBtn, { ae_once: aeOnceSeq, ae_force_enable: 0 },
                            'Auto-exposure run requested (standalone only)');
            },
        }, icon('bullseye'), 'Run AE Once');

        const fmtExpGain = (us, cdb) => {
            if (!us) return '--';
            let s = us + ' \u00b5s  (' + Math.round(1e6 / us) + ' FPS max)';
            if (cdb != null) s += '  \u00b7  ' + (cdb / 100).toFixed(1) + ' dB';
            return s;
        };
        const fmtAeState = (act, e, g, once, conv) => {
            if (act == null || act < 0) return '--';
            if (!act) return 'OFF (manual)';
            const ax = [];
            if (e) ax.push('exposure');
            if (g) ax.push('gain');
            const axes = ax.length ? ' (' + ax.join(' + ') + ')' : '';
            const sett = (conv == null || conv < 0) ? '' : (conv ? ' \u00b7 settled' : ' \u00b7 adjusting\u2026');
            return (once ? 'ON \u00b7 once' : 'ON') + axes + sett;
        };
        const aeLiveExp = el('input', { class: 'input', id: 'im_exp_live', type: 'text',
            disabled: 'disabled',
            title: 'Live exposure the camera is actually using right now. Read-only.',
            style: 'width: 16rem;' });

        const aeState = el('input', { class: 'input', id: 'im_ae_state', type: 'text',
            disabled: 'disabled',
            title: 'Whether auto-exposure is actually running right now (the real '
                 + 'state the camera acts on, regardless of who set it). Read-only.',
            style: 'width: 14rem;' });

        const awbState = el('input', { class: 'input', id: 'im_awb_state', type: 'text',
            disabled: 'disabled',
            title: 'Whether auto-white-balance is actually running right now (the '
                 + 'real state the camera acts on, regardless of who set it). Read-only.',
            style: 'width: 14rem;' });
        const fmtAwbState = (act) => (act == null || act < 0) ? '--'
                                   : (act ? 'ON' : 'OFF (manual)');

        const awbGains = el('input', { class: 'input', id: 'im_awb_gains', type: 'text',
            disabled: 'disabled',
            title: 'The white-balance channel gains actually applied right now '
                 + '(R and B relative to Green=1.00). Auto result while AWB runs, '
                 + 'else the manual/preset value. Read-only.',
            style: 'width: 14rem;' });
        const fmtAwbGains = (r, b) => (r == null || r < 0 || b == null || b < 0) ? '--'
                                   : ('R ' + (r / 1000).toFixed(2) + '  \u00b7  B ' + (b / 1000).toFixed(2));

        const aePanel = panel({ css: 'width: 30rem;' },
            panelTitle('Auto Exposure'),
            el('div', { style: 'color:#888; font-size:0.85em; margin-bottom:0.5rem;' },
                'AE on/off belongs to the streaming client. A USB/GigE host\'s ' +
                'ExposureAuto / GainAuto is always absolute and applies the moment ' +
                'it arrives. "Force Auto-Exposure" is a development / debugging aid ' +
                'and the way to run AE for Standalone HDMI, where no host is ' +
                'connected. Last setting wins. Stored; applies within a second or ' +
                'two - no reboot needed.'),
            labeled('Auto Exposure', aeState),
            labeled('AE Calculated Exposure', aeLiveExp),
            labeled('Force Auto-Exposure', forceAe),
            labeled('AE target brightness (%)', sliderFor(aeTarget, 0, 100, 1)),
            labeled('AE speed (1-100)', sliderFor(aeSpeed, 1, 100, 1)),
            labeled('Metering mode', meter),
            labeled('Priority', aePrio),
            labeled('Highlight protection', aeHi),
            labeled('Anti-flicker', aeFlick),
            labeled('Run AE once (then hold)', aeOnceBtn),
            el('div', { class: 'labeled', style: 'justify-content: end; gap: 0.5rem; margin-top: 0.5rem;' },
                aeReset, aeSave),
        );

        const ispInfo = el('div', { style: 'display:grid; align-items:baseline; '
            + 'grid-template-columns: max-content max-content 1fr max-content max-content; '
            + 'gap:0.12rem 0.6rem; margin:0.05rem 0 0.15rem; font-size:0.9em;' });
        const renderIsp = (isp) => {
            isp = isp || {};
            const metered = (isp.grid_cols || 0) > 0;
            const lab = (t, title) => el('div',
                { style: 'color:var(--color-text); white-space:nowrap;', title }, t);
            const val = (t, title) => el('div',
                { style: 'color:var(--color-text); white-space:nowrap;', title }, t);
            const gap = () => el('div', {});
            ispInfo.innerHTML = '';
            const add = (...n) => n.forEach(x => ispInfo.appendChild(x));
            add(lab('AWB engine',
                    'Whether the FPGA gray-world AWB engine is present. Detected from the '
                  + 'live stats, so unknown until the camera has streamed a frame.'),
                el('div', { style: 'grid-column:2 / -1; color:var(--color-text); white-space:nowrap;' },
                   metered ? (isp.gen_awb ? 'available' : 'not built')
                           : 'not yet detected \u2014 stream to detect'));
            add(lab('Stats grid', 'Metering grid the FPGA reports (needs a live frame).'),
                val(metered ? (isp.grid_cols + '\u00d7' + isp.grid_rows) : '--'),
                gap(),
                lab('Histogram', 'FPGA luma histogram bin count, or off if not built.'),
                val(metered ? (isp.gen_hist ? (isp.hist_bins + ' bins') : 'off') : '--'));
            add(lab('Gray gate', 'Per-sensor default gate that selects "gray" pixels for AWB.'),
                val('thr ' + (isp.awb_gray_thr|0) + ' \u00b7 ' + (isp.awb_gray_en ? 'on' : 'off')
                  + ' \u00b7 Y ' + (isp.awb_y_lo|0) + '\u2013' + (isp.awb_y_hi|0)),
                gap(),
                lab('Gain clamp', 'Per-sensor default min-max clamp on the WB gains.'),
                val(((isp.awb_gain_min_x100|0)/100).toFixed(2) + '\u2013'
                  + ((isp.awb_gain_max_x100|0)/100).toFixed(2)));
            add(lab('AWB rate', 'Per-sensor default auto-WB convergence rate (IIR damping).'),
                val(((isp.awb_damp_x100|0)/100).toFixed(2)));
        };
        const ispPanel = panel({ css: 'width: 30rem;' },
            panelTitle('AWB'),
            el('div', { style: 'color:#888; font-size:0.85em; margin-bottom:0.1rem;' },
                'AWB on/off belongs to the streaming client. A USB/GigE host\'s '
              + 'BalanceWhiteAuto is always absolute. "Force Auto-White-Balance" is '
              + 'a development / debugging aid and the way to run AWB for Standalone '
              + 'HDMI - independent of auto-exposure. Last setting wins.'),
            el('div', { style: 'color:var(--color-text); font-size:0.8em; margin:0 0 0.1rem;' },
                'Imaging pipeline (read-only)'),
            ispInfo,
            el('div', { style: 'margin-top:0.15rem;' }),
            labeled('Auto White Balance', awbState),
            labeled('AWB gains (R/B)', awbGains),
            labeled('Force Auto-White-Balance', forceAwb),
            labeled('Colour temperature', awbCt),
            labeled('AWB min gain', awbMin),
            labeled('AWB max gain', awbMax),
            labeled('AWB rate (1-100)', sliderFor(awbRate, 1, 100, 1)),
            el('div', { class: 'labeled', style: 'justify-content: end; gap: 0.5rem; margin-top: 0.5rem;' },
                awbReset, awbSave),
        );

        p.appendChild(el('div', {
            class: 'container',
            style: 'gap: 0.5rem; align-items: flex-start; flex-wrap: wrap;',
        }, aePanel, ispPanel));

        try {
            const d = await apiGet('/api/imaging');
            aeTarget.value = Math.round((d.ae_target_permille || 0) / 10);
            meter.value    = String((d.ae_metering_mode != null) ? d.ae_metering_mode : 1);
            aeSpeed.value  = d.ae_speed ? d.ae_speed : 40;
            aePrio.value   = String((d.ae_priority  != null) ? d.ae_priority  : 0);
            aeHi.value     = String((d.ae_highlight != null) ? d.ae_highlight : 1);
            aeFlick.value  = String((d.ae_flicker  != null) ? d.ae_flicker : 0);
            aeOnceSeq      = (typeof d.ae_once === 'number') ? d.ae_once : 0;
            forceAe.checked = !!(d.ae_force_enable);
            forceAwb.checked = !!(d.awb_force_enable);
            if (d.isp && d.isp.awb_ct_calibrated != null) {
                const cal = d.isp.awb_ct_calibrated | 0;
                [['32', 0], ['48', 1], ['56', 2], ['65', 3]].forEach(([v, bit]) => {
                    const o = awbCt.querySelector('option[value="' + v + '"]');
                    if (!o) return;
                    if (o.dataset.base == null) o.dataset.base = o.textContent;
                    const p = o.dataset.base.indexOf(' (');
                    const bare = (p > 0) ? o.dataset.base.slice(0, p) : o.dataset.base;
                    o.textContent = ((cal >> bit) & 1) ? o.dataset.base
                                                       : bare + ' - No CAL';
                });
            }
            bakedRate = (d.isp && d.isp.awb_damp_x100) ? d.isp.awb_damp_x100 : 15;
            if (d.isp) {
                awbMin.placeholder = ((d.isp.awb_gain_min_x100|0) / 100).toFixed(2);
                awbMax.placeholder = ((d.isp.awb_gain_max_x100|0) / 100).toFixed(2);
            }
            awbMin.value  = d.awb_gain_min_x100 ? (d.awb_gain_min_x100 / 100).toFixed(2) : '';
            awbMax.value  = d.awb_gain_max_x10  ? (d.awb_gain_max_x10  / 10).toFixed(1)  : '';
            awbRate.value = d.awb_rate_x100 ? d.awb_rate_x100 : bakedRate;
            awbCt.value   = String(d.awb_color_temp_d100 || 0);
            [aeTarget, aeSpeed, awbRate].forEach(n => n.dispatchEvent(new Event('input')));
            aeLiveExp.value = fmtExpGain(d.exposure_us, d.gain_cdb);
            aeState.value   = fmtAeState(d.ae_active, d.ae_exposure_auto, d.ae_gain_auto, d.ae_once_running, d.ae_converged);
            awbState.value  = fmtAwbState(d.awb_active);
            awbGains.value  = fmtAwbGains(d.awb_ratio_r_x1000, d.awb_ratio_b_x1000);
            renderIsp(d.isp);
            const pollExp = async () => {
                if (!document.body.contains(aeLiveExp)) return;
                try {
                    const e = await apiGet('/api/imaging');
                    aeLiveExp.value = fmtExpGain(e.exposure_us, e.gain_cdb);
                    aeState.value   = fmtAeState(e.ae_active, e.ae_exposure_auto, e.ae_gain_auto, e.ae_once_running, e.ae_converged);
                    awbState.value  = fmtAwbState(e.awb_active);
                    awbGains.value  = fmtAwbGains(e.awb_ratio_r_x1000, e.awb_ratio_b_x1000);
                    renderIsp(e.isp);
                } catch (_) {  }
                setTimeout(pollExp, 1500);
            };
            setTimeout(pollExp, 1500);
        } catch (e) {
            toast('Imaging load failed: ' + (e && e.message ? e.message : e), 'error');
        }
    },
};

const pageHdmi = {
    help: {
        tips: [
            'Standalone HDMI is for running with NO computer attached (bring-up, focusing, field use); turn it off to hand the camera back to a USB/GigE host.',
            'In standalone there is no separate exposure control: the HDMI fps sets the exposure (~1/fps). The "Exposure (from fps)" box shows what the chosen fps will apply.',
            'Standalone needs auto-exposure enabled from the camera itself - no host is there to enable it. Turn on "Force Auto-Exposure" on the ISP Control tab.',
            'Turning HDMI off entirely stops the display VDMA and frees DDR bandwidth - worth doing if you are pushing a high-rate USB/GigE stream and nothing is plugged into the HDMI port.',
            'The crop only applies when the sensor is bigger than 1920x1080: HDMI shows a 1:1 window of the frame (there is no scaler). Auto-centre puts that window in the middle; turn it off to pan by typing the start coordinate in sensor pixels.',
        ],
    },
    refreshMs: 0,

    async render() {
        const p = $('#page');
        p.innerHTML = '';

        const hdmiFps  = el('input', { class: 'input', id: 'im_fps', type: 'number',
            title: 'HDMI frame rate, 1-60 fps. Standalone HDMI runs the sensor at ' +
                   'this exact rate; with a USB/GigE host it caps the HDMI preview.',
            min: '1', max: '60', step: '1', style: 'width: 8rem;' });
        const cropX    = el('input', { class: 'input', id: 'im_cx', type: 'number',
            title: 'HDMI crop start X, in sensor pixels (left edge of the 1920x1080 '
                 + 'window). Only used when Auto-centre is off. Clamped to the '
                 + 'sensor width - 1920.',
            min: '0', max: '8191', step: '1', style: 'width: 8rem;' });
        const cropY    = el('input', { class: 'input', id: 'im_cy', type: 'number',
            title: 'HDMI crop start Y, in sensor pixels (top edge of the 1920x1080 '
                 + 'window). Only used when Auto-centre is off. Clamped to the '
                 + 'sensor height - 1080.',
            min: '0', max: '8191', step: '1', style: 'width: 8rem;' });
        const cropAuto = el('input', { type: 'checkbox', class: 'toggle', id: 'im_crop_auto',
            title: 'Centre the 1920x1080 HDMI window on the sensor frame. On by '
                 + 'default. Turn it off to type an exact start coordinate below.' });
        const syncCropDep = () => {
            cropX.disabled = cropAuto.checked;
            cropY.disabled = cropAuto.checked;
        };
        cropAuto.addEventListener('change', syncCropDep);
        const hdmiEnable = el('input', { type: 'checkbox', class: 'toggle', id: 'im_hdmi_en',
            title: 'Master HDMI output switch. When OFF, the HDMI port shows nothing '
                 + 'regardless of USB/Ethernet streaming, and the display VDMA is '
                 + 'stopped to free DDR bandwidth. Persisted across reboots.' });
        const sahdmi   = el('input', { type: 'checkbox', class: 'toggle', id: 'im_sahdmi',
            title: 'Start the camera at 1920x1080 (binned) so HDMI shows live video '
                 + 'when no USB/Ethernet host is connected. A connected host always '
                 + 'takes over. Requires HDMI Enabled. Persisted across reboots.' });
        const syncHdmiDep = () => {
            sahdmi.disabled = !hdmiEnable.checked;
            if (!hdmiEnable.checked) sahdmi.checked = false;
        };
        hdmiEnable.addEventListener('change', syncHdmiDep);

        const fpsToExposureUs = (fps) =>
            Math.round(950000 / Math.max(1, Math.min(60, fps || 60)));

        const hdmiSave = el('button', { class: 'button',
            title: 'Save the HDMI settings (applies within ~1-2 s).',
            onclick: () => {
                const fps = Math.max(1, Math.min(60, parseInt(hdmiFps.value, 10) || 60));
                saveImaging(hdmiSave, {
                    hdmi_enable: hdmiEnable.checked ? 1 : 0,
                    hdmi_standalone: (hdmiEnable.checked && sahdmi.checked) ? 1 : 0,
                    hdmi_max_fps: fps,
                    hdmi_crop_auto: cropAuto.checked ? 1 : 0,
                    hdmi_crop_x:  Math.max(0, parseInt(cropX.value, 10) || 0),
                    hdmi_crop_y:  Math.max(0, parseInt(cropY.value, 10) || 0),
                    exposure_us:  fpsToExposureUs(fps),
                });
            },
        }, icon('save'), 'Save');

        const hdmiExp = el('input', { class: 'input', id: 'im_exp', type: 'text',
            disabled: 'disabled',
            title: 'Exposure time this HDMI fps will use in standalone (auto-computed from fps, in \u00b5s). '
                 + 'One frame = exposure time + blanking time, so the frame rate sets the exposure: '
                 + 'lower fps -> longer exposure, higher fps -> shorter. Read-only.',
            style: 'width: 9rem;' });
        const fmtExp = (us) => us ? us + ' \u00b5s' : '--';
        hdmiFps.addEventListener('input', () => {
            hdmiExp.value = fmtExp(fpsToExposureUs(parseInt(hdmiFps.value, 10) || 60));
        });

        const hdmiState = el('input', { class: 'input', id: 'im_hdmi_state', type: 'text',
            disabled: 'disabled',
            title: 'Whether the HDMI output is actually running now, and whether the '
                 + 'stream is a host (USB/GigE) or the standalone free-run. Read-only.',
            style: 'width: 14rem;' });
        const fmtHdmiState = (en, active, owner) => {
            if (en == null || en < 0) return '--';
            if (!en) return 'OFF (disabled)';
            if (active == null || active < 0) return 'ON (enabled)';
            if (!active) return 'ON \u00b7 idle (no source)';
            if (owner === 1) return 'ON \u00b7 streaming (standalone)';
            if (owner === 2) return 'ON \u00b7 streaming (host)';
            return 'ON \u00b7 streaming';
        };

        const hdmiPanel = panel({ css: 'width: 30rem;' },
            panelTitle('HDMI Controls'),
            el('div', { style: 'color:#888; font-size:0.85em; margin-bottom:0.5rem;' },
                'Turn off "HDMI Enabled" to disable the HDMI output entirely (overrides ' +
                'USB/Ethernet and frees DDR bandwidth). Standalone HDMI requires HDMI ' +
                'Enabled; when using it, also enable "Force Auto-Exposure" (Auto Exposure ' +
                'panel on the ISP Control tab) so the preview auto-adjusts brightness.'),
            labeled('HDMI Status', hdmiState),
            labeled('HDMI Enabled', hdmiEnable),
            labeled('Standalone HDMI Enabled', sahdmi),
            labeled('HDMI FPS', sliderFor(hdmiFps, 1, 60, 1)),
            labeled('Exposure (from fps)', hdmiExp),
            labeled('Auto-centre crop', cropAuto),
            labeled('HDMI crop start X', cropX),
            labeled('HDMI crop start Y', cropY),
            el('div', { class: 'labeled', style: 'justify-content: end; margin-top: 0.5rem;' },
                hdmiSave),
        );

        p.appendChild(el('div', {
            class: 'container',
            style: 'gap: 0.5rem; align-items: flex-start; flex-wrap: wrap;',
        }, hdmiPanel));

        try {
            const d = await apiGet('/api/imaging');
            hdmiFps.value  = d.hdmi_max_fps ? d.hdmi_max_fps : 60;
            cropAuto.checked = (d.hdmi_crop_auto != null) ? !!d.hdmi_crop_auto : true;
            cropX.value    = (d.hdmi_crop_x  != null) ? d.hdmi_crop_x  : 0;
            cropY.value    = (d.hdmi_crop_y  != null) ? d.hdmi_crop_y  : 0;
            hdmiEnable.checked = (d.hdmi_enable != null) ? !!d.hdmi_enable : false;
            sahdmi.checked = !!(d.hdmi_standalone);
            hdmiFps.dispatchEvent(new Event('input'));
            syncHdmiDep();
            syncCropDep();
            hdmiState.value = fmtHdmiState(d.hdmi_enable, d.stream_active, d.stream_owner);
            const pollState = async () => {
                if (!document.body.contains(hdmiState)) return;
                try {
                    const e = await apiGet('/api/imaging');
                    hdmiState.value = fmtHdmiState(e.hdmi_enable, e.stream_active, e.stream_owner);
                } catch (_) {  }
                setTimeout(pollState, 1500);
            };
            setTimeout(pollState, 1500);
        } catch (e) {
            toast('HDMI load failed: ' + (e && e.message ? e.message : e), 'error');
        }
    },
};

function colCss(basis, extra) {
    let css = 'flex: 1 1 0; flex-basis: ' + basis + '; min-width: 2rem; white-space: normal; word-break: break-word;';
    if (extra) css += ' ' + extra;
    return css;
}
function actionColCss() {
    return 'flex: 0 0 14.5rem;';
}
function actionBtnCss() {
    return 'flex: 0 0 7rem; white-space: nowrap; margin-right: 0.25rem; justify-content: center;';
}

const pageUsbFw = {
    help: {
        tips: [
            'Don\u2019t power off or unplug while programming \u2014 it writes the USB controller\u2019s EEPROM.',
            'Add and delete get slower as the library grows \u2014 each change re-validates the entry and re-packs the whole library archive (xz) in the firmware store, so large libraries take longer.',
        ],
    },
    busy: false,
    refreshMs() { return this.busy ? 1000 : 30000; },

    async render() {
        const p = $('#page');
        p.innerHTML = '';

        const headerRow = el('div', { class: 'container' },
            el('div', { style: colCss('14rem'), title: "Entry name (from manifest.name in manifest.json)" }, 'Name'),
            el('div', { style: colCss('3rem'),  title: 'USB firmware version' }, 'Ver'),
            el('div', { style: colCss('4rem'),  title: 'USB protocol this firmware speaks (U3V or UVC)' }, 'Protocol'),
            el('div', { style: colCss('5rem'),  title: 'Compressed archive size' }, 'Size'),
            el('div', { style: colCss('10rem'), title: 'Free-text description from the manifest' }, 'Description'),
            el('div', { style: colCss('9rem'),  title: 'Build date and the git commit the firmware was built from' }, 'Build / SHA'),
            el('div', { style: colCss('5rem'),  title: 'Whether this entry is the firmware currently flashed to the USB controller' }, 'Status'),
            el('div', { style: actionColCss(), title: 'Activate or delete this entry' }, ''),
        );
        const headerWrap = el('div', { style: 'font-weight: 600;' }, headerRow);
        const tbody = el('div', { class: 'container table', id: 'uf_tbody' });
        const libPanel = panel({ css: 'padding-bottom: 30px;' },
            libraryTitle('USB Controller Firmware Library'),
            headerWrap, tbody,
        );
        p.appendChild(libPanel);

        const statusPanel = panel({ css: 'margin-top: 0.5rem; width: 36rem;' },
            panelTitle('USB Controller EEPROM'),
            labeled('Currently loaded', valueSpan('uf_cl', '(none)'),
                    { title: 'USB firmware currently flashed to the controller EEPROM' }),
            labeledTwo('Phase',    valueSpan('uf_ph', '-'),
                       'Progress', valueSpan('uf_pc', '0%'),
                       'Current step of an in-progress flash/activation',
                       'Flash/activation progress'),
            el('div', { style: 'background:#e0e0e0; border-radius:4px; height:14px; overflow:hidden; margin-top:0.2rem;' },
                el('div', { id: 'uf_bar', style: 'background:#4a90e2; height:14px; width:0%; transition:width 0.3s ease;' }),
            ),
            el('div', { id: 'uf_le',  style: 'color: #c00; font-size: 0.9em; min-height: 1.2em;' }, ''),
            el('div', { id: 'uf_lue', style: 'color: #c00; font-size: 0.85em; min-height: 1.2em; font-style: italic;' }, ''),
            el('div', { id: 'uf_lun', style: 'color: #555; font-size: 0.85em; min-height: 1.2em;' }, ''),
            el('div', { id: 'uf_unl', style: 'color: #a07a00; font-size: 0.85em; min-height: 1.2em;', title: 'Archive files in this library that are damaged or not a valid entry, so they are not listed. Uploading the same archive again replaces a damaged one; the Logs page names it.' }, ''),
        );
        p.appendChild(el('div', {
            class: 'container',
            style: 'gap: 0.5rem; align-items: stretch; flex-wrap: wrap;',
        },
            statusPanel,
            uploadWidget({
                store:    'usb_fw',
                width:    '26rem',
                accept:   '.xz',
                titleBar: 'Upload new firmware',
                label:    'Add a .xz archive to the library',
                title:    'select USB firmware .xz file',
                helper:   'File will appear in the table above on successful upload.',
            }),
        ));

        await this.refresh();
    },

    async refresh() {
        try {
            const lib = await apiGet('/api/usb_firmware');
            const items = (lib && lib.items) || [];
            const tbody = $('#uf_tbody');
            if (tbody) {
                tbody.innerHTML = '';
                for (const it of items) {
                    const row = el('div', { class: 'container',
                        style: 'border-top: 1px solid #eee; align-items: center;' },
                        el('div', { style: colCss('14rem'), title: it.name }, it.name),
                        el('div', { style: colCss('3rem'), title: 'USB firmware version' }, it.version),
                        el('div', { style: colCss('4rem'), title: 'USB protocol (U3V or UVC)' }, (it.protocol || '').toUpperCase()),
                        el('div', { style: colCss('5rem'), title: it.size_bytes + ' bytes' }, humanSize(it.size_bytes)),
                        el('div', { style: colCss('10rem', 'font-size: 0.9em; line-height: 1.2; margin-right: 0.75rem;'), title: it.description || '' }, truncDesc(it.description, 64)),
                        el('div', { style: colCss('9rem', 'font-family: monospace; font-size: 0.85em;'), title: 'Build timestamp . git SHA . * if dirty' },
                            (it.build_date || '') + ' ' + (it.git_sha || '') + (it.git_dirty ? '*' : '')),
                        el('div', { style: colCss('5rem', 'font-style: italic; color: #666;'),
                            title: it.is_current ? 'Currently flashed to the controller'
                                 : (it.is_factory ? 'Factory entry (re-heals on reset)' : '') },
                            it.is_current ? 'active' : (it.is_factory ? 'factory' : '')),
                        actionButton('Program', 'import',
                            () => apiPost('/api/program_usb_fw', { name: it.name }).then(() => toast('Program started')).catch(e => toast(e.message, 'error')),
                            { css: actionBtnCss() }),
                        actionButton('Delete', 'inbox',
                            (ev) => deleteEntry(ev.currentTarget, '/api/delete_usb_fw', it.name),
                            { css: actionBtnCss(), disabled: it.is_factory }),
                    );
                    tbody.appendChild(row);
                }
            }
        } catch (e) {  }

        try {
            const st = await apiGet('/api/usb_program_status');
            this.busy = !!(st && st.phase && st.phase !== 'idle');
            setText('uf_cl', fmt.none(st.currently_loaded));
            setText('uf_ph', st.phase || '-');
            setText('uf_pc', fmt.pct(st.percent));
            const bar = document.getElementById('uf_bar');
            if (bar) bar.style.width = (st.percent || 0) + '%';
            setText('uf_le',  st.last_error || '');
            setText('uf_lue', st.last_upload_error || '');
            setText('uf_lun', st.last_upload_note ? ('Note: ' + st.last_upload_note) : '');
            setText('uf_unl', unlistedText(st.unlisted));
        } catch (_) {}
    },
};

const pageBitstream = {
    help: {
        tips: [
            'Steps: (1) prepare a .xz containing bitstream.bin and manifest.json at the top level (overlay.dtbo optional); (2) Upload; (3) Activate; (4) watch the Phase / Progress bar below until it completes.',
            'Activate applies LIVE \u2014 the FPGA is reprogrammed immediately, and video runs on the new fabric without a reboot. This is the opposite of the Sensor Library, where activation only stages the change for the next boot.',
            'A reboot is still RECOMMENDED after a successful activation. The live reprogram swaps the fabric but does not re-probe the drivers bound to it: they were matched against the previous design, so a PL block whose address or configuration moved can end up driven by a driver that no longer fits. Rebooting re-probes everything from the device tree. Not required \u2014 many bitstream pairs differ in ways nothing is bound to \u2014 which is why the panel recommends it rather than forcing it.',
            'Live apply cuts both ways: no reboot is needed to activate, and equally no reboot will undo a bad one. To back out, activate a known-good entry again.',
            'Activate is REFUSED while the camera is streaming. Reprogramming the FPGA pulls the PL out from under the video pipeline mid-transfer, which can wedge the internal bus and leave the next reboot needing a power cycle. Stop acquisition first, then activate.',
            'If the live reprogram fails, the bitstream is not lost: it has already been written to the file the FPGA loads at every boot, so it is STAGED. The panel below shows the reason it failed and offers a Reboot now button \u2014 rebooting applies it through the normal boot path, which does not have to reprogram a running FPGA.',
            'Bitstream and sensor load together at boot \u2014 a mismatched pair can leave the camera with no video. The check is the Sensor Library entry\u2019s WIRED lanes (how its board is physically routed) against the lane count(s) in this row: the sensor\u2019s Bit/Lane is only what the part CAN do, so a sensor listing "2,4" tells you nothing about which of the two its board actually uses. Bit depth must match outright. If Wired shows a dash the archive does not declare it, and only the weaker Bit/Lane comparison is possible.',
            'Known issue: rebooting shortly after a live bitstream activation can stall the reset and need a power cycle. Activating while stopped (which is now enforced) avoids the usual trigger, but if you do hit it, power-cycle the camera.',
            'Add and delete get slower as the library grows \u2014 each change re-validates the entry and re-packs the whole library archive (xz) in the firmware store, so large libraries take longer.',
        ],
    },
    busy: false,
    refreshMs() { return this.busy ? 1000 : 30000; },

    async render() {
        const p = $('#page');
        p.innerHTML = '';

        const headerRow = el('div', { class: 'container' },
            el('div', { style: colCss('14rem'), title: "Entry name (from manifest.name in manifest.json)" }, 'Name'),
            el('div', { style: colCss('3rem'),  title: 'Bitstream version' }, 'Ver'),
            el('div', { style: colCss('6rem'),  title: 'Pixel bit depth (one per bitstream \u2014 the datapath is synthesised for a single width) / the MIPI CSI-2 lane counts this bitstream can receive. Several, e.g. "2,4", if it was built with Active Lanes.' }, 'Bit/Lane'),
            el('div', { style: colCss('8rem'),  title: 'Maximum sensor resolution this bitstream supports' }, 'Max Res'),
            el('div', { style: colCss('12rem'), title: 'FPGA features in this build (e.g. AE, CCM, gamma)' }, 'Features'),
            el('div', { style: colCss('5rem'),  title: 'Compressed archive size' }, 'Size'),
            el('div', { style: colCss('10rem'), title: 'Free-text description from the manifest' }, 'Description'),
            el('div', { style: colCss('9rem'),  title: 'Build date and the git commit the bitstream was built from' }, 'Build / SHA'),
            el('div', { style: colCss('5rem'),  title: 'Whether this entry is the bitstream currently loaded in the FPGA' }, 'Status'),
            el('div', { style: actionColCss(), title: 'Activate or delete this entry' }, ''),
        );
        const tbody = el('div', { class: 'container table', id: 'bs_tbody' });
        p.appendChild(panel({ css: 'padding-bottom: 30px;' },
            libraryTitle('FPGA Bitstream Library'),
            el('div', { style: 'font-weight: 600;' }, headerRow),
            tbody,
        ));

        p.appendChild(el('div', {
            class: 'container',
            style: 'gap: 0.5rem; align-items: stretch; flex-wrap: wrap;',
        },
            panel({ css: 'margin-top: 0.5rem; width: 36rem;' },
                panelTitle('FPGA Bitstream Activation'),
                el('div', { id: 'bs_reboot_banner' }),
                labeled('Currently loaded', valueSpan('bs_cl', '(none)'),
                        { title: 'FPGA bitstream currently loaded (applies live, no reboot)' }),
                labeledTwo('Phase',    valueSpan('bs_ph', '-'),
                           'Progress', valueSpan('bs_pc', '0%'),
                           'Current step of an in-progress activation',
                           'Activation progress'),
                el('div', { style: 'background:#e0e0e0; border-radius:4px; height:14px; overflow:hidden; margin-top:0.2rem;' },
                    el('div', { id: 'bs_bar', style: 'background:#4a90e2; height:14px; width:0%; transition:width 0.3s ease;' }),
                ),
                el('div', { style: 'display:flex; align-items:flex-start; gap:0.75rem; margin-top:0.2rem;' },
                    el('div', { style: 'flex: 1 1 auto; min-width: 0;' },
                        el('div', { id: 'bs_ok',  style: 'font-size: 0.9em; min-height: 1.2em;' }),
                        el('div', { id: 'bs_le',  style: 'color: #c00; font-size: 0.9em; min-height: 1.2em;' }),
                        el('div', { id: 'bs_lue', style: 'color: #c00; font-size: 0.85em; min-height: 1.2em; font-style: italic;' }),
                        el('div', { id: 'bs_lw',  style: 'color: #a07a00; font-size: 0.85em; min-height: 1.2em; font-style: italic;' }),
                        el('div', { id: 'bs_lun', style: 'color: #555; font-size: 0.85em; min-height: 1.2em;' }),
                        el('div', { id: 'bs_unl', style: 'color: #a07a00; font-size: 0.85em; min-height: 1.2em;', title: 'Archive files in this library that are damaged or not a valid entry, so they are not listed. Uploading the same archive again replaces a damaged one; the Logs page names it.' }),
                    ),
                    el('button', { id: 'bs_reboot', class: 'button',
                        style: 'flex: 0 0 auto; display: none;',
                        onclick: () => apiPost('/api/reboot', {}).then(() => toast('Rebooting...')).catch(e => toast(e.message, 'error')) },
                        icon('reboot'), 'Reboot now'),
                ),
            ),
            uploadWidget({
                store:    'bitstream',
                width:    '30rem',
                accept:   '.xz',
                titleBar: 'Upload new bitstream package',
                label:    'Add a .xz bitstream archive to the library',
                title:    'select bitstream .xz file',
                helper:   "Archive must contain bitstream.bin and manifest.json at the top level.  overlay.dtbo is optional.  Entry name comes from manifest.name in manifest.json; re-uploading an archive with the same name replaces the existing entry.",
            }),
        ));

        await this.refresh();
    },

    async refresh() {
        try {
            const lib = await apiGet('/api/bitstream');
            const items = (lib && lib.items) || [];
            const tbody = $('#bs_tbody');
            if (tbody) {
                tbody.innerHTML = '';
                for (const it of items) {
                    const row = el('div', { class: 'container',
                        style: 'border-top: 1px solid #eee; align-items: center;' },
                        el('div', { style: colCss('14rem'), title: it.name }, it.name),
                        el('div', { style: colCss('3rem'), title: 'Bitstream version' }, it.version),
                        el('div', { style: colCss('6rem'),  title: "Pixel Bit Depth and Number of Lanes" },
                            (it.bits != null ? it.bits : '') + ' / ' + (it.lanes != null ? it.lanes : '')),
                        el('div', { style: colCss('8rem'),  title: 'Max Resolution FPGA Bitstream Supports' }, it.max_res || ''),
                        el('div', { style: colCss('12rem'), title: 'FPGA Firmware Features/implemented Modules' }, it.features || ''),
                        el('div', { style: colCss('5rem'), title: it.size_bytes + ' bytes' }, humanSize(it.size_bytes)),
                        el('div', { style: colCss('10rem', 'font-size: 0.9em; line-height: 1.2; margin-right: 0.75rem;'), title: it.description || '' }, truncDesc(it.description, 64)),
                        el('div', { style: colCss('9rem',  'font-family: monospace; font-size: 0.85em;'), title: 'Build timestamp . git SHA . * if dirty' },
                            (it.build_date || '') + ' ' + (it.git_sha || '') + (it.git_dirty ? '*' : '')),
                        el('div', { style: colCss('5rem',  'font-style: italic; color: #666;'),
                            title: (it.is_current ? 'Currently loaded bitstream. ' : '')
                                 + (it.is_factory ? 'Factory baseline (re-seeds on reset)' : '') },
                            (it.is_current ? 'active' : '')
                            + (it.is_factory ? (it.is_current ? ', factory' : 'factory') : '')),
                        actionButton('Activate', 'import',
                            () => apiPost('/api/activate_bitstream', { name: it.name }).then(() => toast('Activation started')).catch(e => toast(e.message, 'error')),
                            { css: actionBtnCss() }),
                        actionButton('Delete', 'inbox',
                            (ev) => deleteEntry(ev.currentTarget, '/api/delete_bitstream', it.name),
                            { css: actionBtnCss(), disabled: it.is_current || it.is_factory,
                              title: (it.is_factory ? 'Factory entries cannot be deleted -- they re-heal on reset. '
                                         : it.is_current ? 'The active entry cannot be deleted. ' : '') }),
                    );
                    tbody.appendChild(row);
                }
            }
        } catch (_) {}

        try {
            const st = await apiGet('/api/bitstream_status');
            this.busy = !!(st && st.phase && st.phase !== 'idle');
            setText('bs_cl', fmt.none(st.currently_loaded));
            setText('bs_ph', st.phase || '-');
            setText('bs_pc', fmt.pct(st.percent));
            const bar = document.getElementById('bs_bar');
            if (bar) bar.style.width = (st.percent || 0) + '%';
            setText('bs_le',  st.reboot_required ? '' : (st.last_error || ''));
            setText('bs_lue', st.last_upload_error || '');
            setText('bs_lw',  st.last_warning || '');
            setText('bs_lun', st.last_upload_note ? ('Note: ' + st.last_upload_note) : '');
            setText('bs_unl', unlistedText(st.unlisted));

            const bsBanner = document.getElementById('bs_reboot_banner');
            const bsRb     = document.getElementById('bs_reboot');
            if (st.reboot_required) {
                if (bsBanner) bsBanner.innerHTML =
                    "<div style='background:#fff3cd; border:1px solid #ffeeba; color:#856404; padding:0.5rem 0.75rem; border-radius:4px; margin-bottom:0.6rem;'>" +
                    "&#9888; Bitstream activation failed" +
                    (st.last_error ? ": " + esc(st.last_error) : ".") +
                    "<br>Bitstream activation is staged. Reboot to activate the bitstream." +
                    "<br><span style='font-size:0.9em;'>Running now: <b>" + esc(st.currently_loaded || '(none)') +
                    "</b> &rarr; Next boot: <b>" + esc(st.requested || '') + "</b></span></div>";
                setText('bs_ok', '');
                if (bsRb) {
                    bsRb.title = 'Reboot the camera now to activate the staged bitstream.';
                    bsRb.style.display = 'inline-flex';
                }
            } else {
                if (bsBanner) bsBanner.innerHTML = '';

                if (st.phase === 'done') {
                    setText('bs_ok',
                        'Activation successful \u2014 the new bitstream is running. '
                        + 'Some PL blocks and/or their drivers may need a reboot to '
                        + 'function properly. Reboot recommended.');
                    if (bsRb) {
                        bsRb.title = 'Reboot the camera to re-probe the PL blocks '
                                   + 'and their drivers against the new bitstream.';
                        bsRb.style.display = 'inline-flex';
                    }
                } else {
                    setText('bs_ok', '');
                    if (bsRb) bsRb.style.display = 'none';
                }
            }
        } catch (_) {}
    },
};

const pageSensor = {
    help: {
        tips: [
            'Steps: (1) prepare a .xz containing manifest.json and sensor.ko at the top level (optional: overlay.dtbo, ccm.bin, gamma.bin, default_config.json, firmware/*.bin); (2) Upload; (3) Activate; (4) Reboot.',
            'Activate only STAGES the change for the next boot \u2014 the running sensor does not change until you restart. The Dashboard shows \u201creboot required\u201d until you do, and the banner above has a Reboot now button. This is the opposite of the Bitstream Library, where activation applies live.',
            'Before rebooting, check this entry\u2019s WIRED lanes against the active bitstream\u2019s lane count(s), and that the bit depths match. Wired is the number that matters \u2014 Bit/Lane is only what the sensor and driver CAN do, so an entry listing "2,4" passes that test against a bitstream it cannot actually talk to. The two load together at boot and a mismatched pair comes up with no video, which is awkward to diagnose because the camera boots fine and simply produces no frames.',
            'Add and delete get slower as the library grows \u2014 each change re-validates the entry and re-packs the whole library archive (xz) in the firmware store, so large libraries take longer.',
        ],
    },
    busy: false,
    refreshMs() { return this.busy ? 1000 : 30000; },

    async render() {
        const p = $('#page');
        p.innerHTML = '';

        const headerRow = el('div', { class: 'container' },
            el('div', { style: colCss('11rem'), title: "Entry name (from manifest.name in manifest.json)" }, 'Name'),
            el('div', { style: colCss('9rem'),  title: 'Sensor vendor and model' }, 'Vendor:Model'),
            el('div', { style: colCss('5rem'),  title: 'Pixel bit depth / MIPI CSI-2 lane count the sensor and driver SUPPORT' }, 'Bit/Lane'),
            el('div', { style: colCss('5rem'),  title: 'MIPI CSI-2 lanes physically ROUTED on this sensor board. Bit/Lane is what the part can do; this is how this particular carrier is wired, and it mirrors the overlay\u2019s data-lanes. A dash means the archive does not declare it.' }, 'Wired'),
            el('div', { style: colCss('3rem'),  title: 'Sensor driver/config version' }, 'Ver'),
            el('div', { style: colCss('7rem'),  title: 'Maximum sensor resolution' }, 'Max Res'),
            el('div', { style: colCss('7rem'),  title: 'Kernel module (.ko) name shipped in the archive' }, 'KMod'),
            el('div', { style: colCss('10rem'), title: 'Free-text description from the manifest' }, 'Description'),
            el('div', { style: colCss('9rem'),  title: 'Build date and the git commit the sensor package was built from' }, 'Build / SHA'),
            el('div', { style: colCss('5rem'),  title: 'Active = staged for the next boot (sensor changes need a reboot)' }, 'Status'),
            el('div', { style: actionColCss(), title: 'Activate or delete this entry' }, ''),
        );
        const tbody = el('div', { class: 'container table', id: 'sn_tbody' });
        p.appendChild(panel({ css: 'padding-bottom: 30px;' },
            libraryTitle('Camera Sensor Library'),
            el('div', { style: 'font-weight: 600;' }, headerRow),
            tbody,
            el('div', { style: 'color: #888; font-size: 0.8em; margin-top: 0.3rem;', html:
                'Opts legend: <span style="font-family:monospace">D</span> = DT overlay, <span style="font-family:monospace">C</span> = CCM, <span style="font-family:monospace">G</span> = Gamma, <span style="font-family:monospace">F</span> = Firmware blobs' }),
        ));

        p.appendChild(el('div', {
            class: 'container',
            style: 'gap: 0.5rem; align-items: stretch; flex-wrap: wrap;',
        },
            panel({ css: 'margin-top: 0.5rem; width: 42rem;' },
                panelTitle('Sensor Activation Status'),
                el('div', { id: 'sn_reboot_banner' }),
                labeledTwo('Active now',       valueSpan('sn_act',  '(none)'),
                           'Installed (next boot)', valueSpan('sn_inst', '(none)'),
                           'Sensor package running right now',
                           'Sensor package staged for the next reboot'),
                labeledTwo('Phase',    valueSpan('sn_ph', '-'),
                           'Progress', valueSpan('sn_pc', '0%'),
                           'Current step of an in-progress activation',
                           'Activation progress'),
                el('div', { style: 'background:#e0e0e0; border-radius:4px; height:14px; overflow:hidden; margin-top:0.2rem;' },
                    el('div', { id: 'sn_bar', style: 'background:#4a90e2; height:14px; width:0%; transition:width 0.3s ease;' }),
                ),
                el('div', { style: 'display:flex; align-items:flex-start; gap:0.75rem; margin-top:0.2rem;' },
                    el('div', { style: 'flex: 1 1 auto; min-width: 0;' },
                        el('div', { id: 'sn_le',  style: 'color: #c00; font-size: 0.9em; min-height: 1.2em;' }),
                        el('div', { id: 'sn_lue', style: 'color: #c00; font-size: 0.85em; min-height: 1.2em; font-style: italic;' }),
                        el('div', { id: 'sn_lw',  style: 'color: #a07a00; font-size: 0.85em; min-height: 1.2em; font-style: italic;' }),
                        el('div', { id: 'sn_lun', style: 'color: #555; font-size: 0.85em; min-height: 1.2em;' }),
                        el('div', { id: 'sn_unl', style: 'color: #a07a00; font-size: 0.85em; min-height: 1.2em;', title: 'Archive files in this library that are damaged or not a valid entry, so they are not listed. Uploading the same archive again replaces a damaged one; the Logs page names it.' }),
                    ),
                    el('button', { id: 'sn_reboot', class: 'button',
                        title: 'Reboot the camera now to apply the staged sensor change.',
                        style: 'flex: 0 0 auto; display: none;',
                        onclick: () => apiPost('/api/reboot', {}).then(() => toast('Rebooting...')).catch(e => toast(e.message, 'error')) },
                        icon('reboot'), 'Reboot now'),
                ),
            ),
            uploadWidget({
                store:    'sensor',
                width:    '34rem',
                accept:   '.xz',
                titleBar: 'Upload new sensor package',
                label:    'Add a .xz sensor archive to the library',
                title:    'select sensor archive .xz file',
                helper:   "Archive must contain manifest.json and sensor.ko at the top level (or inside a single wrapper directory).  Optional files: overlay.dtbo, ccm.bin, gamma.bin, default_config.json, firmware/*.bin.  Entry name comes from manifest.name in manifest.json; re-uploading an archive with the same name replaces the existing entry.  A successful activation stages files for the next boot - the new sensor only becomes active after rebooting.",
            }),
        ));

        await this.refresh();
    },

    async refresh() {
        let st = {};
        try { st = await apiGet('/api/sensor_status'); } catch (_) {}
        this.busy = !!(st && st.phase && st.phase !== 'idle');
        try {
            const lib = await apiGet('/api/sensor');
            const items = (lib && lib.items) || [];
            const tbody = $('#sn_tbody');
            if (tbody) {
                tbody.innerHTML = '';
                for (const it of items) {
                    const baseStatus =
                        it.name === st.currently_active ? 'active' :
                        (it.is_current ? 'next boot' : '');
                    const statusTxt = baseStatus
                        + (it.is_factory ? (baseStatus ? ', factory' : 'factory') : '');
                    const row = el('div', { class: 'container',
                        style: 'border-top: 1px solid #eee; align-items: center;' },
                        el('div', { style: colCss('11rem'), title: it.name }, it.name),
                        el('div', { style: colCss('9rem'), title: (it.vendor || '') + ':' + (it.sensor_model || '') }, (it.vendor || '') + ':' + (it.sensor_model || '')),
                        el('div', { style: colCss('5rem'),
                            title: (it.bits != null ? it.bits + '-bit' : '')
                                 + (it.lanes != null
                                    ? ' / supports ' + it.lanes + ' lane'
                                      + (String(it.lanes).indexOf(',') >= 0 ? 's' : '')
                                    : '') },
                            (it.bits != null ? it.bits : '') + ':' + (it.lanes != null ? it.lanes : '')),
                        el('div', { style: colCss('5rem'),
                            title: it.lanes_wired ? it.lanes_wired + ' lane(s) routed on this sensor board'
                                                  : 'This archive does not declare how many lanes its board routes' },
                            it.lanes_wired ? String(it.lanes_wired) : '\u2013'),
                        el('div', { style: colCss('3rem'), title: 'Sensor driver/config version' }, it.version),
                        el('div', { style: colCss('7rem'), title: 'Maximum sensor resolution' }, it.max_res || ''),
                        el('div', { style: colCss('7rem',  'font-family: monospace; font-size: 0.85em;'), title: 'Kernel Module File' }, it.kernel_module || ''),
                        el('div', { style: colCss('10rem', 'font-size: 0.9em; line-height: 1.2; margin-right: 0.75rem;'), title: it.description || '' }, truncDesc(it.description, 64)),
                        el('div', { style: colCss('9rem',  'font-family: monospace; font-size: 0.85em;'), title: 'Build timestamp . git SHA . * if dirty' },
                            (it.build_date || '') + ' ' + (it.git_sha || '') + (it.git_dirty ? '*' : '')),
                        el('div', { style: colCss('5rem',  'font-style: italic; color: #666;'),
                            title: 'current = running now; active = staged for next boot' }, statusTxt),
                        actionButton('Activate', 'import',
                            () => apiPost('/api/activate_sensor', { name: it.name }).then(() => toast('Activation staged')).catch(e => toast(e.message, 'error')),
                            { css: actionBtnCss() }),
                        actionButton('Delete', 'inbox',
                            (ev) => deleteEntry(ev.currentTarget, '/api/delete_sensor', it.name),
                            { css: actionBtnCss(), disabled: it.is_current || it.is_factory,
                              title: (it.is_factory ? 'Factory entries cannot be deleted -- they re-heal on reset. '
                                         : it.is_current ? 'The active entry cannot be deleted. ' : '') }),
                    );
                    tbody.appendChild(row);
                }
            }
        } catch (_) {}

        setText('sn_act',  fmt.none(st.currently_active));
        setText('sn_inst', fmt.none(st.currently_installed));
        setText('sn_ph',   st.phase || '-');
        setText('sn_pc',   fmt.pct(st.percent));
        const bar = document.getElementById('sn_bar');
        if (bar) bar.style.width = (st.percent || 0) + '%';
        setText('sn_le',  st.last_error || '');
        setText('sn_lue', st.last_upload_error || '');
        setText('sn_lw',  st.last_warning || '');
        setText('sn_lun', st.last_upload_note ? ('Note: ' + st.last_upload_note) : '');
        setText('sn_unl', unlistedText(st.unlisted));

        const banner = document.getElementById('sn_reboot_banner');
        const rb     = document.getElementById('sn_reboot');
        if (st.reboot_required) {
            if (banner) banner.innerHTML =
                "<div style='background:#fff3cd; border:1px solid #ffeeba; color:#856404; padding:0.5rem 0.75rem; border-radius:4px; margin-bottom:0.6rem;'>" +
                "&#9888; A sensor change has been staged. Reboot the camera to apply." +
                "<br><span style='font-size:0.9em;'>Active now: <b>" + (st.currently_active || '(none)') +
                "</b> &rarr; Next boot: <b>" + (st.currently_installed || '') + "</b></span></div>";
            if (rb) rb.style.display = 'inline-flex';
        } else {
            if (banner) banner.innerHTML = '';
            if (rb) rb.style.display = 'none';
        }
    },
};

const pageNetwork = {
    help: {
        tips: [
            'Factory Reset restores network, imaging and the admin password to factory, makes the factory sensor and bitstream active, and re-flashes the USB firmware \u2014 then reboots. Your uploaded libraries are KEPT, unless you also tick "Delete user data", which erases all uploaded sensor / bitstream / USB-FW archives (leaving only factory firmware).',
            'Takes ~1 minute to perform the factory reset, and the system will auto-reboot. Do not remove power during the reset.',
            'Apply, both saves the network settings and switches to them immediately \u2014 changing the IP will drop this connection, so reconnect at the new address.',
        ],
    },
    refreshMs: 0,
    async render() {
        const p = $('#page');
        p.innerHTML = '';

        const ifaceBanner = el('div', { id: 'ns_iface_banner' });
        p.appendChild(ifaceBanner);

        const row = el('div', { class: 'container',
            style: 'flex-wrap: wrap; gap: 0.5rem; align-items: start;' });

        const curIp     = el('div', { class: 'value', id: 'ns_cur_ip'  }, '-');
        const curIpSrc  = el('div', { class: 'value', id: 'ns_cur_src' }, '-');
        const curMask   = el('div', { class: 'value', id: 'ns_cur_nm'  }, '-');
        const curGw     = el('div', { class: 'value', id: 'ns_cur_gw'  }, '-');
        const curMac    = el('div', { class: 'value', id: 'ns_cur_mac' }, '-');
        const curLink   = el('div', { class: 'value', id: 'ns_cur_lk'  }, '-');
        const curMtu    = el('div', { class: 'value', id: 'ns_cur_mtu' }, '-');
        const curSpeed  = el('div', { class: 'value', id: 'ns_cur_sp'  }, '-');
        const curRxBy   = el('div', { class: 'value', id: 'ns_rx_by'   }, '-');
        const curTxBy   = el('div', { class: 'value', id: 'ns_tx_by'   }, '-');
        const livePanel = panel({ css: 'width: 22rem;' },
            panelTitle('Network - Live'),
            labeled('Current IP',      curIp),
            labeled('IP source',       curIpSrc),
            labeled('Current netmask', curMask),
            labeled('Current gateway', curGw),
            labeled('MAC address',     curMac),
            labeled('Link',            curLink),
            labeled('Link speed',      curSpeed),
            labeled('MTU',             curMtu),
            labeled('RX bytes',        curRxBy),
            labeled('TX bytes',        curTxBy),
        );
        row.appendChild(livePanel);

        const dhcp = el('input', { type: 'checkbox', class: 'toggle', id: 'ns_dhcp',
            title: 'On: get IP automatically (DHCP). Off: use the static IP below.' });
        const ip   = el('input', { class: 'input', id: 'ns_ip',  style: 'width: 10rem;',
            title: 'Static IPv4 address, e.g. 192.168.1.100 (used when DHCP is off).' });
        const nm   = el('input', { class: 'input', id: 'ns_nm',  style: 'width: 10rem;',
            title: 'Subnet mask, e.g. 255.255.255.0.' });
        const gw   = el('input', { class: 'input', id: 'ns_gw',  style: 'width: 10rem;',
            title: 'Default gateway IPv4 address, e.g. 192.168.1.1.' });
        const uname = el('input', { class: 'input', id: 'ns_un', maxlength: '15',
            title: 'Device hostname (max 15 characters).',
            style: 'width: 10rem;', placeholder: 'chc5-cam' });

        const updateDisabled = () => {
            ip.disabled = nm.disabled = gw.disabled = dhcp.checked;
        };
        dhcp.addEventListener('change', updateDisabled);

        const buildBody = (apply) => {
            const body = {
                mode: dhcp.checked ? 'dhcp' : 'static',
                user_name: uname.value || '',
                apply: apply,
            };
            if (!dhcp.checked) {
                body.ip      = ip.value;
                body.netmask = nm.value;
                body.gateway = gw.value;
            }
            return body;
        };

        const applyBtn = el('button', { class: 'button',
            title: 'Persist settings to disk and switch the IP live. ' +
                   'Connection may drop if your IP changes.',
            onclick: async () => {
                const ok = confirm(
                    'Apply network settings?\n\n' +
                    'Settings will be saved and the camera will switch ' +
                    'to the new IP now.  You may lose the connection ' +
                    'and need to reconnect at the new address.');
                if (!ok) return;
                applyBtn.disabled = true;
                applyBtn.textContent = 'applying...';
                try {
                    await apiPost('/api/network', buildBody(true));
                    toast('Applied. Reconnect at new address if needed.');
                } catch (e) {
                    toast(e.message + ' (settings may still have applied)',
                          'error');
                } finally {
                    applyBtn.disabled = false;
                    applyBtn.innerHTML = '';
                    applyBtn.appendChild(icon('save'));
                    applyBtn.appendChild(document.createTextNode('Apply'));
                }
            },
        }, icon('save'), 'Apply');

        const cfgPanel = panel({ css: 'width: 22rem;' },
            panelTitle('Network - Settings'),
            labeled('DHCP', dhcp),
            labeled('IP address',     ip),
            labeled('Netmask',        nm),
            labeled('Gateway',        gw),
            labeled('Device name',    uname),
            el('div', { class: 'labeled',
                style: 'margin-top: 0.25rem; justify-content: end; gap: 0.5rem;' },
                applyBtn,
            ),
        );
        row.appendChild(cfgPanel);

        const tipWrap = (btn, tip) =>
            el('span', { title: tip,
                style: 'display:inline-block;justify-self:end;' }, btn);

        const cbLabel = (cb, text) =>
            el('label', { style: 'display:inline-flex;align-items:center;'
                               + 'gap:0.4rem;cursor:pointer;white-space:nowrap;' },
                cb, el('span', {}, text));

        const FR_DETAIL = 'Factory reset: restores network (DHCP), imaging and the '
                        + 'admin password to factory, makes the factory bitstream / '
                        + 'sensor active, re-flashes the USB-controller firmware to '
                        + 'factory, then reboots (~1 min). Your uploaded library '
                        + 'archives are KEPT \u2014 unless you also tick "Delete user '
                        + 'data", which erases them (leaving only factory firmware).';

        const FR_OFF = 'Tick "I\'m sure" to enable';
        const FR_ON  = 'Factory reset and reboot the camera';
        const frArm = el('input', { type: 'checkbox', id: 'fr_arm',
            title: 'Tick to confirm \u2014 enables the Factory Reset button' });
        const frDel = el('input', { type: 'checkbox', id: 'fr_del', disabled: true,
            title: 'Also erase ALL uploaded sensor / bitstream / USB-FW archives '
                 + '(leaves only factory firmware)' });
        const frBtn = el('button', { class: 'button', disabled: true,
            onclick: async () => {
                if (!frArm.checked) return;
                frBtn.disabled = true;
                const wipe = frDel.checked;
                if (wipe) {
                    try { await apiPost('/api/clear_library', { scope: 0x3F }); }
                    catch (e) {
                        toast('Deleting user data failed (' + (e && e.message ? e.message : e)
                            + ') - resetting anyway', 'error');
                    }
                }
                factoryResetAndReboot(wipe
                    ? 'Factory reset + deleting user data \u2014 rebooting...'
                    : 'Factory reset applied \u2014 rebooting...');
            },
        }, icon('factory'), 'Factory Reset');
        const frWrap = tipWrap(frBtn, FR_OFF);
        const frLast = el('div', { id: 'fr_last',
            style: 'font-size:0.85em;color:#555;max-width:34rem;' }, '');
        apiGet('/api/dashboard').then(d => {
            const r = d && d.factory_reset;
            if (!r) return;
            let t = 'Last factory reset: ' + (r.result === 'completed' ? 'completed'
                  : r.result === 'pending' ? 'not finished yet - it completes at the next boot'
                  : 'did not complete' + (r.error ? ' (' + r.error + ')' : ''));
            if (r.freed) t += '. To make space it removed: ' + r.freed;
            frLast.textContent = t + '.';
        }).catch(() => {});
        frArm.addEventListener('change', () => {
            frBtn.disabled = !frArm.checked;
            frWrap.title    = frArm.checked ? FR_ON : FR_OFF;
            frDel.disabled  = !frArm.checked;
            if (!frArm.checked) frDel.checked = false;
        });

        const rbBtn = el('button', { class: 'button',
            onclick: () => {
                apiPost('/api/reboot', {})
                    .then(() => toast('Rebooting...'))
                    .catch(e => toast(e.message, 'error'));
            }
        }, icon('reboot'), 'Reboot');
        const rbWrap = tipWrap(rbBtn, 'Reboot the camera now');

        const dlBtn = el('button', { class: 'button',
            onclick: async () => {
                dlBtn.disabled = true;
                try {
                    const res = await fetch('/api/var_backup',
                        { headers: { 'Authorization': 'Bearer ' + authToken } });
                    if (!res.ok) throw new Error('HTTP ' + res.status);
                    const blob = await res.blob();
                    const url  = URL.createObjectURL(blob);
                    const a    = el('a', { href: url, download: 'userdata-backup.tar.gz' });
                    document.body.appendChild(a); a.click(); a.remove();
                    URL.revokeObjectURL(url);
                    toast('Backup downloaded', 'ok');
                } catch (e) {
                    toast('Backup failed: ' + (e && e.message ? e.message : e), 'error');
                } finally {
                    dlBtn.disabled = false;
                }
            },
        }, icon('save'), 'Download Backup');
        const dlWrap = tipWrap(dlBtn,
            'Download a backup of user data (config + firmware libraries) as a '
            + '.tar.gz. Contains secrets (password hash, keys) - handle carefully.');

        const actPanel = panel({ css: 'width: fit-content;' },
            panelTitle('Device Actions'),
            el('div', { style: 'display:grid;grid-template-columns:auto auto;'
                             + 'align-items:center;row-gap:0.9rem;column-gap:1.5rem;' },
                el('div', { class: 'label', title: 'Download a backup of user data '
                    + '(config + firmware libraries) as a .tar.gz' }, 'Backup User Data'),
                dlWrap,
                el('div', { style: 'grid-column:1 / -1;display:flex;flex-direction:column;'
                                 + 'gap:0.6rem;margin:0.3rem 0;' },
                    el('div', { style: 'display:flex;align-items:center;gap:1.3rem;flex-wrap:wrap;' },
                        el('span', { class: 'label', title: FR_DETAIL,
                                     style: 'font-weight:600;' }, 'Factory Reset'),
                        cbLabel(frArm, "I'm sure"),
                        cbLabel(frDel, 'Delete user data'),
                    ),
                    el('div', { style: 'display:flex;justify-content:flex-end;' }, frWrap),
                    frLast,
                ),
                el('div', { class: 'label' }, 'Reboot Device'),
                rbWrap,
            ),
        );
        row.appendChild(actPanel);

        const timeNow = el('div', { class: 'value', id: 'ns_time_now' }, '-');
        const timePanel = panel({ css: 'width: 30rem;' },
            panelTitle('Date & Time'),
            labeled('Device time (UTC)', timeNow),
            el('div', { class: 'label', style: 'margin-top: 0.2rem; opacity: 0.75;',
                html: 'No Battery Backed RTC on Camera - the clock resets to the build date each '
                    + 'boot. Set it here to fix log timestamps and keep signed updates '
                    + 'valid.' }),
        );
        if (authRole === 'admin') {
            const applyEpoch = async (epoch, okMsg) => {
                try {
                    await apiPost('/api/system/time', { epoch });
                    toast(okMsg || 'Device time set');
                    try {
                        const t = await apiGet('/api/system/time');
                        timeNow.textContent = t.iso || '-';
                    } catch (_) {}
                } catch (e) { toast(e.message, 'error'); }
            };
            const manualInput = el('input', { type: 'datetime-local', step: '1',
                class: 'input', id: 'ns_time_manual',
                title: 'Set the device clock manually (UTC). Used when there is no NTP/RTC.',
                style: 'flex: 0 1 auto; min-width: 0;' });
            manualInput.value = new Date().toISOString().slice(0, 19);
            timePanel.appendChild(el('div', {
                style: 'margin-top: 0.7rem; display:flex; gap:0.5rem; '
                     + 'align-items:center; flex-wrap:wrap;' },
                el('button', { class: 'button',
                    title: "Set the device clock to this computer's current time",
                    onclick: () => applyEpoch(Math.floor(Date.now() / 1000),
                                              'Synced to this computer') },
                    icon('clock'), ' Sync to this computer')));
            timePanel.appendChild(el('div', {
                style: 'margin-top: 0.5rem; display:flex; gap:0.5rem; '
                     + 'align-items:center; flex-wrap:wrap;' },
                el('span', { class: 'label' }, 'Manual (UTC):'),
                manualInput,
                el('button', { class: 'button',
                    title: 'Apply the manually entered UTC date & time to the device clock.',
                    onclick: () => {
                        const v = manualInput.value;
                        if (!v) { toast('Pick a date and time first', 'error'); return; }
                        const ms = new Date(v + 'Z').getTime();
                        if (isNaN(ms)) { toast('Invalid date/time', 'error'); return; }
                        applyEpoch(Math.floor(ms / 1000), 'Device time set');
                    } }, 'Set')));
        }
        row.appendChild(timePanel);

        p.appendChild(row);

        try {
            const t = await apiGet('/api/system/time');
            timeNow.textContent = t.iso || '-';
        } catch (_) {}

        const dispIp = v => (v && v !== '0.0.0.0') ? v : '';
        try {
            const d = await apiGet('/api/network');
            dhcp.checked = (d.mode === 'dhcp');
            ip.value     = dispIp(d.ip);
            nm.value     = dispIp(d.netmask);
            gw.value     = dispIp(d.gateway);
            uname.value  = d.user_name || '';
            curIp.textContent   = d.current_ip      || '-';
            curMask.textContent = d.current_netmask || '-';
            curGw.textContent   = d.current_gateway || '-';
            curMac.textContent  = d.mac             || '-';
            curLink.textContent = d.link_up ? 'up' : 'down';
            curMtu.textContent = (typeof d.mtu === 'number' && d.mtu > 0)
                ? String(d.mtu) : '-';
            if (typeof d.speed_mbps === 'number' && d.speed_mbps > 0) {
                curSpeed.textContent = d.speed_mbps + ' Mbps' +
                    (d.duplex ? ' ' + d.duplex : '');
            } else {
                curSpeed.textContent = '-';
            }

            const curIpStr = d.current_ip || '';
            if (!curIpStr || curIpStr === '0.0.0.0') {
                curIpSrc.textContent = '-';
            } else if (curIpStr.startsWith('169.254.')) {
                curIpSrc.textContent = 'Link-Local (LLA fallback)';
            } else if (d.mode === 'static') {
                curIpSrc.textContent = 'Persistent IP (static)';
            } else if (d.mode === 'dhcp') {
                curIpSrc.textContent = 'DHCP (lease)';
            } else {
                curIpSrc.textContent = 'Unknown';
            }

            const fmtBytes = (n) => {
                if (typeof n !== 'number' || n < 0) return '-';
                if (n < 1024)            return n + ' B';
                if (n < 1024*1024)       return n + ' B (' + (n/1024).toFixed(1) + ' KiB)';
                if (n < 1024*1024*1024)  return n + ' B (' + (n/(1024*1024)).toFixed(1) + ' MiB)';
                return n + ' B (' + (n/(1024*1024*1024)).toFixed(2) + ' GiB)';
            };
            curRxBy.textContent = fmtBytes(d.rx_bytes);
            curTxBy.textContent = fmtBytes(d.tx_bytes);
            updateDisabled();

            const detected = (d.iface_detected !== false);
            if (!detected) {
                ifaceBanner.innerHTML =
                    "<div style='background:#f8d7da;border:1px solid #f5c2c7;" +
                    "color:#842029;padding:0.5rem 0.75rem;border-radius:4px;" +
                    "margin-bottom:0.75rem;font-size:0.9em;'>" +
                    "&#9888; <b>No Ethernet interface detected.</b> " +
                    "Platformd's startup scan found neither <code>macb</code> " +
                    "nor <code>axienet</code> bound to a network port. " +
                    "Settings can still be saved, but Apply will fail until " +
                    "the FPGA bitstream is loaded and the link is up." +
                    "</div>";
                applyBtn.disabled = true;
                applyBtn.title = 'No Ethernet interface detected - ' +
                                 'cannot apply live until a macb/axienet ' +
                                 'port is present.';
            } else {
                ifaceBanner.innerHTML = '';
                applyBtn.disabled = false;
            }
        } catch (e) {
            toast('Network info load failed: ' +
                  (e && e.message ? e.message : String(e)), 'error');
            try { console.error('pageNetwork: /api/network failed', e); }
            catch (_) {}
        }
    },
    refresh() {},
};

const pageLog = {
    refreshMs: 0,

    _filter: { service: '', lines: 30 },

    async render() {
        const p = $('#page');
        p.innerHTML = '';

        const groups = [
            ['System',     [['system', 'System (full log)']]],
            ['Daemons',    [['chc5_platformd', 'chc5_platformd'], ['chc5_webd', 'chc5_webd'],
                            ['camcfgd', 'camcfgd'], ['gvcp_server', 'gvcp_server']]],
            ['Boot tasks', [['chc5-clock-floor', 'chc5-clock-floor'], ['chc5-rwlayers', 'chc5-rwlayers'],
                            ['chc5-machine-id', 'chc5-machine-id'], ['chc5-hostname', 'chc5-hostname'],
                            ['chc5-netgen', 'chc5-netgen'], ['chc5-provision', 'chc5-provision'],
                            ['chc5-sensor-load', 'chc5-sensor-load'], ['chc5-mark-good', 'chc5-mark-good'],
                            ['chc5-factory-check', 'chc5-factory-check'], ['chc5-var-prepare', 'chc5-var-prepare']]],
        ];
        const svc = el('select', { class: 'select', id: 'lg_svc', style: 'width: 14rem;',
            title: 'Choose which service or daemon to show log output for.' });
        for (const [label, items] of groups) {
            const og = el('optgroup', { label });
            for (const [val, lbl] of items.slice().sort((a, b) => a[1].localeCompare(b[1])))
                og.appendChild(el('option', { value: val }, lbl));
            svc.appendChild(og);
        }
        svc.value = this._filter.service || 'chc5_webd';

        const lines = el('input', { class: 'input', id: 'lg_lines',
            type: 'number', min: '10', max: '1000', step: '10',
            title: 'How many of the most recent log lines to fetch (10-1000).',
            style: 'flex: 0 0 auto; width: 5rem;',
            value: String(this._filter.lines) });

        const summary = el('div', { id: 'lg_summary',
            style: 'color: #666; font-size: 0.85rem; margin-top: 0.5rem;' });

        const filterPanel = panel(null,
            panelTitle('Filter'),
            el('div', { class: 'container', style: 'gap: 1rem; align-items: center;' },
                el('div', { class: 'labeled', style: 'gap: 0.5rem;' },
                    el('div', { class: 'label' }, 'Service'), svc),
                el('div', { class: 'labeled', style: 'gap: 0.5rem;' },
                    el('div', { class: 'label' }, 'Lines'), lines),
                el('button', { class: 'button',
                    title: 'Apply the service / line-count filter and reload the log.',
                    onclick: () => this._apply() },
                    icon('save'), 'Apply'),
                el('button', { class: 'button',
                    title: 'Re-fetch the latest log lines for the selected service.',
                    onclick: () => this._apply() },
                    icon('refresh'), 'Refresh'),
            ),
            summary,
        );
        p.appendChild(filterPanel);

        const headerRow = el('div', { class: 'container',
            style: 'gap: 0.5rem; align-items: center;' },
            el('div', { style: 'width: 3rem; text-align: right;' }, '#'),
            el('div', { style: 'width: 11rem;' }, 'Timestamp'),
            el('div', { style: 'flex: 1 1 auto;' }, 'Message'),
        );
        const tbody = el('div', { class: 'container table', id: 'lg_tbody',
            style: 'flex: 1 1 auto; overflow: auto; font-family: ui-monospace, Menlo, Consolas, monospace; font-size: 0.78rem; line-height: 1.35;' });

        const outPanel = panel({ css: 'flex: 1 1 auto; overflow: hidden; display: flex; flex-direction: column;' },
            panelTitle('Output'),
            el('div', { style: 'font-weight: 600; padding: 0.25rem 0.5rem; border-bottom: 1px solid #ddd; flex: 0 0 auto;' }, headerRow),
            tbody,
        );
        p.appendChild(outPanel);

        await this._apply();
    },

    async _apply() {
        const svc = $('#lg_svc');
        const ln  = $('#lg_lines');
        const summary = $('#lg_summary');
        if (svc) this._filter.service = svc.value;
        if (ln)  this._filter.lines   = Math.min(1000, Math.max(10, parseInt(ln.value, 10) || 30));

        const svcLabel = (this._filter.service === 'system')
            ? 'System (full log)' : this._filter.service;
        if (summary) summary.innerHTML = 'Showing the last <b>' + this._filter.lines +
            '</b> lines of <b>' + svcLabel + '</b>, newest first. Click Apply / Refresh to reload.';

        try {
            const q = new URLSearchParams();
            q.set('n', String(this._filter.lines));
            if (this._filter.service && this._filter.service !== 'system')
                q.set('filter', this._filter.service);
            const d = await apiGet('/api/log?' + q.toString());
            const lines = (d && d.lines) || [];
            const tbody = $('#lg_tbody');
            if (!tbody) return;
            tbody.innerHTML = '';
            for (let i = lines.length - 1; i >= 0; i--) {
                const ln = lines[i];
                let ts = '', msg = ln;
                const m = ln.match(/^(\w{3}\s+\d+\s+\d{2}:\d{2}:\d{2})\s+(.*)$/);
                if (m) { ts = m[1]; msg = m[2]; }
                tbody.appendChild(el('div', { class: 'container',
                    style: 'gap: 0.5rem; align-items: baseline; padding: 0.15rem 0.5rem; border-top: 1px solid #f0f0f0;' },
                    el('div', { style: 'width: 3rem; text-align: right; color: #999;' }, String(i + 1)),
                    el('div', { style: 'width: 11rem; color: #555; white-space: nowrap;' }, ts),
                    el('div', { style: 'flex: 1 1 auto; white-space: pre-wrap; word-break: break-word;' }, msg),
                ));
            }
            if (lines.length === 0)
                tbody.appendChild(el('div', {
                    style: 'padding: 0.6rem 0.5rem; color: #999; font-style: italic;' },
                    'No log entries for this service in the current boot.'));
        } catch (e) {
            toast('Log: ' + e.message, 'error');
        }
    },

    refresh() {},
};

const pageFirmwareUpdate = {
    help: {
        tips: [
            'A/B update with auto-rollback \u2014 if the new image fails to boot, the camera falls back to the working slot on its own.',
            'Only signed bundles for this device and storage type (SD vs QSPI) are accepted. The upload doesn\u2019t interrupt operation; the reboot to activate does.',
            'Current camera configuration \u2014 including the active bitstream, sensor, and USB firmware \u2014 is kept. An update writes to the inactive A/B slot and only refreshes the factory baseline; it does not change what is running. Use Factory Restore to switch to the updated factory version.',
            'After an A/B update auto-rollback \u2014 the camera reverting because the new firmware would not run stably \u2014 the libraries can look empty on the rolled-back boot. If the update also brought a new library structure or version, the older firmware may not understand a library list written by the newer one, so the Sensor, Bitstream and USB Firmware pages can show only the factory entry. Nothing has been deleted: the entries are still stored on the camera, and the lists fill in again once you are back on the newer firmware.',
            'If library or user-stored firmware appears to be missing, never use Factory Restore to fix an empty-looking library. The entries are unlisted, not gone; a Factory Restore with "delete user data" removes them for real. To keep a copy first, use Device Settings \u2192 Download Backup \u2014 it saves everything on the camera, including entries that are not being listed.',
        ],
    },
    refreshMs: 2000,

    parseShell: (text) => {
        const o = {};
        (text || '').split('\n').forEach((line) => {
            const m = line.match(/^([A-Z0-9_]+)='(.*)'$/);
            if (m) o[m[1]] = m[2];
        });
        return o;
    },

    render: async () => {
        const p = $('#page');
        p.innerHTML = '';

        let fileBtn;
        const fi = el('input', {
            type: 'file', accept: '.raucb', style: 'display:none;',
            onchange: async (e) => {
                const f = e.target.files && e.target.files[0];
                e.target.value = '';
                if (!f) return;
                await uploadFile(f, 'rauc', fileBtn);
                await pageFirmwareUpdate.loadInspect();
            },
        });
        fileBtn = el('button', { class: 'button',
            title: 'Choose a signed .raucb update bundle to upload and inspect.',
            onclick: () => fi.click() },
            icon('import'), ' Select update bundle (.raucb)');

        const root = panel({},
            panelTitle('System Firmware Update'),
            el('div', { style: 'padding: 0.5rem 0.75rem; color:#666;' },
                'Upload a signed update bundle (.raucb) to review its contents before ' +
                'applying.  The update is written to the inactive A/B slot, verified against ' +
                'the on-device signing key, and activated on the next reboot - with automatic ' +
                'rollback if the new slot fails to boot.'),
            el('div', { style: 'padding: 0.25rem 0.75rem; color:#475569; font-size: 0.85em;' },
                'Expected flash time: QSPI ~5 minutes, SD upgrade ~2 minutes.'),
            el('div', { class: 'labeled', style: 'padding: 0.25rem 0.75rem;' },
                el('div', { class: 'label' }, 'Current system version:'),
                el('div', { class: 'value', id: 'fw_cur_ver' }, '-')),
            el('div', { class: 'labeled', style: 'padding: 0.5rem 0.75rem;' },
                el('div', { class: 'label' }, 'Update bundle:'),
                fileBtn),
            fi,
            el('div', { id: 'rauc_status', style: 'padding: 0.5rem 0.75rem;' }),
        );
        p.appendChild(root);

        p.appendChild(panel({ css: 'margin-top:0.6rem;' },
            panelTitle('System Slots (A / B)'),
            el('div', { id: 'rauc_slots', style: 'padding: 0.5rem 0.75rem; color:#666;' }, '...')));

        try {
            const d = await apiGet('/api/dashboard');
            setText('fw_cur_ver', (d.versions && d.versions.chc5) || '-');
            pageFirmwareUpdate._devBootEnvGen =
                parseInt((d.versions && d.versions.boot_env_gen) || '0', 10) || 0;
            pageFirmwareUpdate._devBootEnv = (d.versions && d.versions.boot_env) || '';
        } catch (_) {}
        pageFirmwareUpdate.loadSlots();

        pageFirmwareUpdate._lastSig = null;
        let st = null;
        try { st = await apiGet('/api/rauc/status'); } catch (_) {}
        if (st && st.phase && st.phase !== 'idle') {
            pageFirmwareUpdate._wasActive = true;
            pageFirmwareUpdate.renderProgress(st);
        } else {
            await pageFirmwareUpdate.loadInspect();
        }
    },

    loadSlots: async () => {
        const box = document.getElementById('rauc_slots');
        if (!box) return;
        let d;
        try { d = await apiGet('/api/rauc/slots'); }
        catch (_) { box.textContent = '(slot status unavailable)'; return; }
        const o = pageFirmwareUpdate.parseShell(d.status || '');
        const curVer = (document.getElementById('fw_cur_ver') || {}).textContent || '';
        box.innerHTML = '';

        const idxs = (o.RAUC_SLOTS || '').trim().split(/\s+/).filter(Boolean);
        const ab = idxs.filter((i) => o['RAUC_SLOT_BOOTNAME_' + i])
            .sort((x, y) => (o['RAUC_SLOT_BOOTNAME_' + x] || '')
                .localeCompare(o['RAUC_SLOT_BOOTNAME_' + y] || ''));

        const isBooted = (i) => (o['RAUC_SLOT_STATE_' + i] || '') === 'booted';
        const bootedIdx = ab.find(isBooted);
        const runBoot = bootedIdx ? o['RAUC_SLOT_BOOTNAME_' + bootedIdx]
                                  : (o.RAUC_SYSTEM_BOOTED_SLOT || '-');
        box.appendChild(el('div', {},
            el('b', {}, 'Running slot: '),
            document.createTextNode(runBoot
                + (bootedIdx && curVer ? '  (' + curVer + ')' : ''))));

        if (ab.length) {
            const grid = el('div', { style: 'margin-top:0.4rem; display:grid; '
                + 'grid-template-columns:auto auto 1fr; gap:0.25rem 1.2rem; font-size:0.9rem;' });
            ['Boot slot', 'State', 'Installed version'].forEach((h) =>
                grid.appendChild(el('div', { class: 'label' }, h)));
            ab.forEach((i) => {
                const booted = isBooted(i);
                let recorded = '';
                ['RAUC_SLOT_BUNDLE_VERSION_' + i,
                 'RAUC_SLOT_STATUS_BUNDLE_VERSION_' + i,
                 'RAUC_SLOT_SLOT_STATUS_BUNDLE_VERSION_' + i].forEach((k) => {
                    if (!recorded && o[k]) recorded = o[k];
                });
                const untracked = !booted && !recorded;
                const ver = booted ? (curVer || '-') : (recorded || 'factory image');
                grid.appendChild(el('div', { style: booted ? 'font-weight:700;' : '' },
                    (o['RAUC_SLOT_BOOTNAME_' + i] || i) + (booted ? ' <- running' : '')));
                grid.appendChild(el('div', {}, o['RAUC_SLOT_STATE_' + i] || '-'));
                grid.appendChild(el('div', {
                    style: untracked ? 'color:#999; font-style:italic;' : '',
                    title: untracked
                        ? 'This slot holds the factory-flashed image. RAUC records a '
                          + 'version here once it installs a web update to this slot.'
                        : '' }, ver));
            });
            box.appendChild(grid);
        } else {
            box.appendChild(el('div', { style: 'color:#888; margin-top:0.3rem;' },
                '(slot details unavailable)'));
        }
    },

    loadInspect: async () => {
        const box = document.getElementById('rauc_status');
        if (!box) return;
        let d;
        try { d = await apiGet('/api/rauc/inspect'); }
        catch (_) { box.innerHTML = ''; return; }

        const o = pageFirmwareUpdate.parseShell(d.info || '');
        box.innerHTML = '';
        const ver = o.RAUC_MF_VERSION;

        if (!ver) {
            box.appendChild(el('div', { style: 'padding:0.5rem 0.6rem; border-radius:4px; '
                + 'background:#fde7e7; color:#a30;' },
                el('b', {}, 'Bundle could not be verified. '),
                document.createTextNode('The signature is invalid, or the device clock is wrong '
                    + '(set it on Device Settings -> Date & Time if you see '
                    + '"certificate not yet valid").')));
            if (d.info)
                box.appendChild(el('pre', { style: 'white-space:pre-wrap; font-size:0.75rem; '
                    + 'color:#900; margin-top:0.3rem;' }, d.info.slice(0, 600)));
            box.appendChild(el('div', { style: 'margin-top:0.5rem;' },
                pageFirmwareUpdate.deleteBtn()));
            return;
        }

        const cur = (document.getElementById('fw_cur_ver') || {}).textContent || '';
        const row = (k, v) => el('div', { style: 'display:flex; gap:0.5rem; margin:0.15rem 0;' },
            el('div', { class: 'label', style: 'min-width:10rem;' }, k),
            el('div', { class: 'value' }, (v && v.length) ? v : '-'));
        const needGen    = parseInt(o.RAUC_META_BOOT_ENV_REQUIRED_GEN || '0', 10) || 0;
        const haveGen    = pageFirmwareUpdate._devBootEnvGen || 0;
        const devBootEnv = pageFirmwareUpdate._devBootEnv || '-';

        const card = el('div', { style: 'border:1px solid #cfcfcf; border-radius:6px; '
            + 'padding:0.6rem 0.8rem; background:#fafafa;' });
        card.appendChild(el('div', { style: 'font-weight:700; font-size:1.05rem; '
            + 'margin-bottom:0.4rem;' }, 'Update ready to review'));
        card.appendChild(row('Current version', cur));
        card.appendChild(row('Update version', ver));
        card.appendChild(row('Current Boot Platform', devBootEnv + (haveGen ? ' [gen ' + haveGen + ']' : '')
            + (needGen ? '  (update needs gen ' + needGen + ')' : '')));
        if (o.RAUC_MF_COMPATIBLE && o.RAUC_MF_COMPATIBLE !== 'chc5')
            card.appendChild(el('div', { style: 'color:#a30; font-weight:600;' },
                '[!] Incompatible model (compatible=' + o.RAUC_MF_COMPATIBLE + ')'));
        if (needGen > haveGen)
            card.appendChild(el('div', { style: 'margin:0.3rem 0; padding:0.4rem 0.6rem; '
                + 'border-radius:4px; background:#fff3cd; color:#8a6d00; font-weight:600;' },
                '[!] This update expects boot-env generation ' + needGen + ', but this device '
                + 'has ' + haveGen + '. The boot environment is reflash-only - after installing, '
                + 're-flash the QSPI/SD boot image so the new boot logic takes effect.'));
        card.appendChild(row('Built', o.RAUC_MF_BUILD));
        if (o.RAUC_MF_DESCRIPTION)
            card.appendChild(el('div', { style: 'margin:0.45rem 0; color:#444;' },
                o.RAUC_MF_DESCRIPTION));

        card.appendChild(el('div', { class: 'label', style: 'margin-top:0.5rem;' },
            'Factory defaults in this image'));
        card.appendChild(row('Bitstream', o.RAUC_META_FACTORY_BITSTREAM));
        card.appendChild(row('Sensor', o.RAUC_META_FACTORY_SENSOR));
        card.appendChild(row('USB firmware', o.RAUC_META_FACTORY_USB_FW));

        const comps = [['linux', 'LINUX'], ['u-boot', 'UBOOT'], ['platformd', 'PLATFORMD'],
            ['webd', 'WEBD'], ['camcfgd', 'CAMCFGD'], ['gvcp', 'GVCP'], ['CHC5 System', 'SUPERPROJECT']];
        const cdiv = el('div', { style: 'margin-top:0.5rem; font-family:monospace; '
            + 'font-size:0.78rem; color:#666;' });
        cdiv.appendChild(el('div', { class: 'label' }, 'Components'));
        comps.forEach(([label, key]) => {
            const v = o['RAUC_META_COMPONENTS_' + key];
            if (v) cdiv.appendChild(el('div', {}, label + ': ' + v));
        });
        card.appendChild(cdiv);

        card.appendChild(el('div', { style: 'margin-top:0.5rem; color:#0a0; font-weight:600;' },
            '[OK] Signature verified'));

        const btns = el('div', { style: 'margin-top:0.7rem; display:flex; gap:0.6rem; '
            + 'flex-wrap:wrap;' });
        if (authRole === 'admin') {
            const apply = el('button', { class: 'button',
                title: 'Install this bundle to the inactive A/B slot; activates on next reboot.',
                onclick: async () => {
                    apply.disabled = true;
                    try {
                        await apiPost('/api/rauc/apply', {});
                        toast('Update started');
                        pageFirmwareUpdate._lastSig = null;
                        pageFirmwareUpdate._wasActive = true;
                    } catch (e) { toast(e.message, 'error'); apply.disabled = false; }
                } }, icon('import'), ' Apply update');
            btns.appendChild(apply);
            btns.appendChild(pageFirmwareUpdate.deleteBtn());
        } else {
            btns.appendChild(el('div', { style: 'color:#888;' },
                'Sign in as admin to apply.'));
        }
        card.appendChild(btns);
        box.appendChild(card);
    },

    deleteBtn: () => el('button', { class: 'button',
        title: 'Discard the uploaded/staged update bundle without installing it.',
        onclick: async () => {
            try {
                await apiPost('/api/rauc/discard', {});
                toast('Staged update deleted');
                const box = document.getElementById('rauc_status');
                if (box) box.innerHTML = '';
            } catch (e) { toast(e.message, 'error'); }
        } }, 'Delete'),

    renderProgress: (st) => {
        const box = document.getElementById('rauc_status');
        if (!box) return;

        const phase   = st.phase || 'idle';
        const pct     = (typeof st.percent === 'number') ? st.percent : 0;
        const rawMsg  = (st.message || '').trim();
        const stepMsg = rawMsg.replace(/^\d+%\s*/, '').trim();

        const prev = pageFirmwareUpdate._prevPhase;
        if ((phase === 'queued' || phase === 'verifying') &&
            (prev === undefined || prev === 'idle' || prev === 'done' || prev === 'failed'))
            pageFirmwareUpdate._log = [];
        pageFirmwareUpdate._prevPhase = phase;
        if (!Array.isArray(pageFirmwareUpdate._log)) pageFirmwareUpdate._log = [];
        const log = pageFirmwareUpdate._log;
        if (stepMsg && (log.length === 0 || log[log.length - 1] !== stepMsg)) {
            log.push(stepMsg);
            if (log.length > 50) log.shift();
        }

        const sig = JSON.stringify([phase, pct, st.reboot_required, log.length]);
        if (sig === pageFirmwareUpdate._lastSig) return;
        pageFirmwareUpdate._lastSig = sig;

        const PHASE_LABEL = {
            queued: 'Queued', verifying: 'Verifying signature',
            installing: 'Installing to inactive slot', done: 'Installed', failed: 'Failed',
        };
        const active = (phase === 'installing' || phase === 'verifying');

        box.innerHTML = '';
        box.appendChild(el('div', {},
            el('b', {}, 'Status: '),
            document.createTextNode((PHASE_LABEL[phase] || phase)
                                    + (active ? ' - ' + pct + '%' : ''))));
        if (active)
            box.appendChild(el('div', { style: 'margin-top:0.4rem; height:10px; '
                + 'background:#e5e5e5; border-radius:5px; overflow:hidden;' },
                el('div', { style: 'height:100%; width:' + pct + '%; '
                    + 'background:#2a9d4a; transition:width 0.4s;' })));
        if (rawMsg)
            box.appendChild(el('div', { style: 'margin-top:0.4rem; color:#333; '
                + 'font-weight:600;' }, rawMsg));
        if (log.length > 1) {
            const logBox = el('div', { style: 'margin-top:0.5rem; max-height:9rem; '
                + 'overflow-y:auto; background:rgba(0,0,0,0.05); border-radius:4px; '
                + 'padding:0.4rem 0.6rem; font-family:monospace; font-size:0.8rem; '
                + 'color:#555; line-height:1.5;' });
            log.forEach((line, i) => logBox.appendChild(el('div', {
                style: (i === log.length - 1) ? 'color:#222; font-weight:600;' : '' },
                (i === log.length - 1 ? '-> ' : '. ') + line)));
            box.appendChild(logBox);
            logBox.scrollTop = logBox.scrollHeight;
        }
        if (phase === 'done' && st.reboot_required) {
            box.appendChild(el('div', { style: 'margin-top:0.5rem; padding:0.4rem 0.6rem; '
                + 'border-radius:4px; background:#0a3; color:#fff;' },
                'Update installed. Reboot to boot the new slot.'));
            const rb = el('button', { class: 'button', style: 'margin-top:0.5rem;',
                title: 'Reboot now to boot the newly installed slot.',
                onclick: async () => {
                    rb.disabled = true;
                    try { await apiPost('/api/reboot', {}); toast('Rebooting...'); }
                    catch (e) { toast('Reboot failed: ' + e.message, 'error'); rb.disabled = false; }
                } }, icon('reboot'), ' Reboot now');
            box.appendChild(rb);
        } else if (phase === 'failed') {
            box.appendChild(el('div', { style: 'margin-top:0.5rem; padding:0.4rem 0.6rem; '
                + 'border-radius:4px; background:#a30; color:#fff;' },
                'Update failed. The active slot is unchanged.'));
        }
    },

    refresh: async () => {
        let st;
        try { st = await apiGet('/api/rauc/status'); }
        catch (_) { return; }
        const phase = st.phase || 'idle';
        if (phase === 'idle') {
            if (pageFirmwareUpdate._wasActive) {
                pageFirmwareUpdate._wasActive = false;
                pageFirmwareUpdate.loadSlots();
                pageFirmwareUpdate.loadInspect();
            }
            return;
        }
        pageFirmwareUpdate._wasActive = true;
        pageFirmwareUpdate.renderProgress(st);
    },
};

const pageAdminPassword = {
    poll: null,
    render: async () => {
        const p = $('#page');
        p.innerHTML = '';

        const oldPw     = el('input', { type: 'password', class: 'input', id: 'pw_old',
            title: 'Your current password.' });
        const newPw     = el('input', { type: 'password', class: 'input', id: 'pw_new',
            title: 'New password to set.' });
        const confirmPw = el('input', { type: 'password', class: 'input', id: 'pw_confirm',
            title: 'Re-type the new password to confirm.' });

        const showPw = el('input', { type: 'checkbox', id: 'pw_show',
            title: 'Show the password fields as plain text to verify what you typed.' });
        showPw.addEventListener('change', () => {
            const t = showPw.checked ? 'text' : 'password';
            oldPw.type = newPw.type = confirmPw.type = t;
        });
        const showRow = el('label', {
            style: 'display:flex; align-items:center; gap:0.4rem; cursor:pointer; '
                 + 'font-size: var(--text-xs); opacity:0.85;',
        }, showPw, 'Show passwords');

        const whoLabel = (authUser ? ' (' + authUser + ')' : '');
        const selfPanel = panel({ css: 'width: 24rem;' },
            panelTitle('Change My Password' + whoLabel),
            el('div', { style: 'padding: 0.5rem 0.75rem; display:flex; flex-direction:column; gap:0.5rem; max-width: 22rem;' },
                el('label', {}, 'Current password'),
                oldPw,
                el('label', {}, 'New password'),
                newPw,
                el('label', {}, 'Confirm new password'),
                confirmPw,
                showRow,
                el('button', {
                    class: 'button',
                    title: 'Save the new password (you will be signed out and must log in again).',
                    style: 'margin-top: 0.5rem; align-self: flex-start;',
                    onclick: async () => {
                        if (newPw.value !== confirmPw.value) {
                            toast('New passwords do not match', 'error');
                            return;
                        }
                        if (!oldPw.value || !newPw.value) {
                            toast('Enter current and new password', 'error');
                            return;
                        }
                        try {
                            await apiPost('/api/admin_password', {
                                old: oldPw.value,
                                new: newPw.value,
                            });
                            toast('Password changed - please log in again', 'ok');
                            oldPw.value = newPw.value = confirmPw.value = '';
                        } catch (e) {
                            toast('Failed: ' + e.message, 'error');
                        }
                    },
                }, icon('save'), ' Save'),
            ),
        );
        p.appendChild(selfPanel);
    },
};

const CAMIO_DEC = {
    in_function:   ['Off', 'Hardware trigger', 'Sync in'],
    in_activation: ['Rising edge', 'Falling edge', 'Any edge', 'Level'],
    strobe_src:    ['PL Calculated', 'Native Strobe'],
    out_source:    ['Off', 'User value', 'Strobe', 'Exposure active', 'Status', 'Sync out', 'Trigger passthrough', 'PTP pulse'],
    role:          ['Off (free-run)', 'Master', 'Slave'],
    role_by:       ['Pin (XMASTER)', 'I2C register'],
    trig_route:    ['XTRIG pin', 'XVS slave', 'I2C register'],
};
function camioLbl(map, v) {
    const a = CAMIO_DEC[map];
    return (a && a[v] !== undefined) ? a[v] : ('? (' + v + ')');
}

const pageIosync = {
    help: {
        intro: 'Aux I/O + multi-camera sync (the chc5_camio_sync FPGA block): hardware trigger in, programmable strobe out, and genlock. GenICam (GigE DigitalIOControl / CHC5AuxSyncControl) or the USB aux block is the PRIMARY path; each panel here saves a persistent fallback that also applies live (last writer wins). The block enables itself automatically when any function is configured.',
        tips: [
            'Not configured / disabled means nothing has driven the block: all muxes off, sensor XVS/XHS buffers Hi-Z, strobe and OPTO-out inactive, so the sensor free-runs.',
            'All delays/durations are microseconds (the hardware resolves to ~10 us).',
            'Sync role: Master drives the shared XVS/XHS out to other cameras; Slave follows an external master; Off = free-run.',
        ],
    },
    refreshMs: 1500,
    async render() {
        const page = $('#page');
        page.innerHTML = '';
        let d = {};
        try { d = await apiGet('/api/iosync'); } catch (_) {}

        const iv = (id) => parseInt(($('#' + id) || {}).value || '0', 10) || 0;
        const cv = (id) => (($('#' + id) || {}).checked ? 1 : 0);
        const num = (id, val, min, max) => el('input', { class: 'input', id, type: 'number',
            min: String(min), max: String(max), step: '1', value: String(val || 0), style: 'width:6rem;' });
        const unum = (id, val) => {
            const inp = el('input', { id, type: 'number', min: '0', max: '10000000', step: '10',
                value: String(val || 0),
                style: 'border:none; background:transparent; outline:none; width:4rem; padding:0; text-align:right; color:inherit; font:inherit;' });
            inp.addEventListener('blur', () => {
                const n = Math.max(0, parseInt(inp.value || '0', 10) || 0);
                inp.value = String(Math.round(n / 10) * 10);
            });
            return el('div', { class: 'input',
                style: 'display:inline-flex; align-items:center; gap:0.15rem; width:6rem;' },
                inp, el('span', { style: 'opacity:0.55; font-size:0.85em;' }, 'us'));
        };
        const chk = (id, on) => el('input', Object.assign({ id, type: 'checkbox', class: 'toggle' }, on ? { checked: 'checked' } : {}));
        const sel = (id, val, opts, dis) => {
            const s = el('select', { class: 'input', id, style: 'width:11.5rem;' });
            opts.forEach((t, i) => s.appendChild(el('option',
                Object.assign({ value: String(i) },
                    i === (val || 0) ? { selected: 'selected' } : {},
                    (dis && dis[i]) ? { disabled: 'disabled' } : {}), t)));
            return s;
        };
        const grp = (t) => el('div', { class: 'label', style: 'font-weight:600; margin:0.5rem 0 0.1rem;' }, t);
        const off = (v) => v === 0;
        const setDis = (id, dis) => {
            const e = $('#' + id); if (!e) return;
            e.disabled = !!dis;
            const box = e.parentElement;
            if (box && box.classList && box.classList.contains('input')) box.classList.toggle('disabled', !!dis);
        };
        const applyModes = () => {
            const pl     = iv('io_strb_src') === 0;
            const role   = iv('io_role');
            const master = role === 1;
            const trig   = iv('io_in_func')  === 1;
            const usr    = iv('io_out_src')  === 1;
            setDis('io_strb_delay', !pl);  setDis('io_strb_dur', !pl);  setDis('io_strb_min', !pl);
            setDis('io_roleby', role === 0);
            setDis('io_in_act', !trig);  setDis('io_in_inv', !trig);
            setDis('io_trig_delay', !trig);  setDis('io_trig_div', !trig);  setDis('io_trigroute', !trig);
            setDis('io_out_uv', !usr);
        };

        const saveIosync = async (btn, body) => {
            btn.disabled = true;
            try { await apiPost('/api/iosync', body); toast('Aux settings saved', 'ok'); }
            catch (e) { toast('Save failed: ' + (e && e.message ? e.message : e), 'error'); }
            finally { btn.disabled = false; }
        };
        const mkSave = (bodyFn) => {
            const b = el('button', { class: 'button' }, icon('save'), 'Save');
            b.addEventListener('click', () => saveIosync(b, bodyFn()));
            return el('div', { class: 'labeled', style: 'justify-content:end; margin-top:0.5rem;' }, b);
        };
        const W = 'width: 23rem;';

        const outPanel = panel({ css: W },
            panelTitle('Aux Output Control'),
            grp('Strobe'),
            labeled('Enable',        chk('io_strb_en', d.strobe_enable),
                { title: 'Enable the strobe / flash output generator.' }),
            labeled('Source',        sel('io_strb_src', d.strobe_src, ['PL Calculated', 'Native Strobe'],
                [false, off(d.cap_has_strobe)]),
                { title: 'PL Calculated = fixed pulse per frame from the timing below. Native = pass the sensor exposure strobe (width follows exposure). Native Strobe is only available on a sensor with a native strobe (FSTROBE/TOUT0).' }),
            labeled('Delay',         unum('io_strb_delay', d.strobe_delay_us),
                { title: 'Frame start (XVS) to strobe rise. 0 = at frame start. Only available when Source = PL Calculated.' }),
            labeled('Duration',      unum('io_strb_dur', d.strobe_duration_us),
                { title: 'Strobe pulse width (0 = no pulse). Only available when Source = PL Calculated.' }),
            labeled('Min on',        unum('io_strb_min', d.strobe_minon_us),
                { title: 'Minimum on-time floor; stretches short pulses. 0 = off. Only available when Source = PL Calculated.' }),
            labeled('Invert',        chk('io_strb_inv', d.strobe_invert),
                { title: 'Invert the strobe active level/edge.' }),
            grp('OPTO_OUT'),
            labeled('Source',        sel('io_out_src', d.out_source,
                ['Off', 'User value', 'Strobe', 'Exposure active', 'Status', 'Sync out', 'Trigger passthrough', 'PTP pulse'],
                [false, false, false, off(d.cap_has_strobe), false, off(d.cap_has_xvs), false, d.cap_ptp_pulse !== 1]),
                { title: 'What signal drives the OPTO_OUT pin. Exposure-active needs a native strobe and Sync-out needs XVS (those options grey out if the sensor lacks them). PTP pulse: a 1 ms pulse on every PTP second (the lock pulses of the FPGA time base), for checking sync between cameras on a scope; needs a bitstream with the PTP time base and the pin-13 wire, and the optocoupler adds microseconds of delay.' }),
            labeled('Invert',        chk('io_out_inv', d.out_invert),
                { title: 'Invert the OPTO_OUT pad to match your opto-isolator. Applies to all sources.' }),
            labeled('User value',    chk('io_out_uv', d.out_user_value),
                { title: 'Manual OPTO_OUT level. Only available when Output Source = User value.' }),
            mkSave(() => ({
                strobe_enable: cv('io_strb_en'), strobe_src: iv('io_strb_src'),
                strobe_delay_us: iv('io_strb_delay'),
                strobe_duration_us: iv('io_strb_dur'), strobe_minon_us: iv('io_strb_min'),
                strobe_invert: cv('io_strb_inv'),
                out_source: iv('io_out_src'), out_invert: cv('io_out_inv'), out_user_value: cv('io_out_uv'),
            })));

        const inPanel = panel({ css: W },
            panelTitle('Aux Input Control'),
            grp('Trigger input (OPTO_IN)'),
            labeled('Function',      sel('io_in_func', d.in_function, ['Off', 'Hardware trigger', 'Sync in'],
                [false, false, off(d.cap_has_xvs)]),
                { title: 'OPTO_IN role: Off, Hardware trigger (drives the sensor), or Sync in (genlock to XVS). Sync in needs a sensor with XVS.' }),
            labeled('Activation',    sel('io_in_act', d.in_activation, ['Rising edge', 'Falling edge', 'Any edge', 'Level']),
                { title: 'Which edge / level of OPTO_IN fires the trigger. Only available when Function = Hardware trigger.' }),
            labeled('Invert',        chk('io_in_inv', d.in_invert),
                { title: 'Invert OPTO_IN polarity before edge detection. Only available when Function = Hardware trigger.' }),
            labeled('Delay',         unum('io_trig_delay', d.trigger_delay_us),
                { title: 'OPTO_IN edge to sensor trigger delay. 0 = immediate. Only available when Function = Hardware trigger.' }),
            labeled('Divider',       num('io_trig_div', d.trigger_divider, 0, 65535),
                { title: 'Fire once every N triggers (1 = every trigger). Only available when Function = Hardware trigger.' }),
            grp('Sync / genlock'),
            labeled('Role',          sel('io_role', d.sync_role, ['Off (free-run)', 'Master', 'Slave'],
                [false, off(d.cap_xvs_out), off(d.cap_xvs_in)]),
                { title: 'Genlock role: Master drives XVS/XHS to other cameras; Slave follows an external master; Off = free-run. Master needs a sensor that can drive XVS; Slave one that can receive it.' }),
            labeled('Role select',   sel('io_roleby', d.sync_role_by, ['Pin (XMASTER)', 'I2C register']),
                { title: 'Choose master/slave via the XMASTER pin or an I2C register. Only used when Role is Master or Slave.' }),
            labeled('Trigger route', sel('io_trigroute', d.sync_trig_route, ['XTRIG pin', 'XVS slave', 'I2C register'],
                [off(d.cap_has_xtrig), off(d.cap_xvs_in), false]),
                { title: 'Which sensor input a trigger is routed to. Only used when Function = Hardware trigger; options grey out if the sensor lacks the pin (e.g. no XTRIG).' }),
            mkSave(() => ({
                in_function: iv('io_in_func'), in_activation: iv('io_in_act'), in_invert: cv('io_in_inv'),
                trigger_delay_us: iv('io_trig_delay'), trigger_divider: iv('io_trig_div'),
                sync_role: iv('io_role'), sync_role_by: iv('io_roleby'),
                sync_trig_route: iv('io_trigroute'),
            })));

        const statusPanel = panel({ css: W },
            panelTitle('Status'),
            el('div', { id: 'io_live' }));

        page.appendChild(el('div', { class: 'container', style: 'gap:0.6rem; align-items:flex-start; flex-wrap:wrap;' },
            outPanel, inPanel, statusPanel));

        ['io_strb_src', 'io_role', 'io_in_func', 'io_out_src'].forEach((id) => {
            const e = $('#' + id); if (e) e.addEventListener('change', applyModes);
        });
        applyModes();

        this._paintLive(d);
    },
    async refresh() {
        try { this._paintLive(await apiGet('/api/iosync')); } catch (_) {}
    },
    _paintLive(d) {
        d = d || {};
        const w = $('#io_live');
        if (!w) return;
        w.innerHTML = '';

        const absent = d.present && d.block_present === 0;
        let stateTxt;
        if (!d.live)         { stateTxt = 'camcfgd not reporting'; }
        else if (absent)     { stateTxt = 'aux block not in this bitstream'; }
        else if (!d.present) { stateTxt = 'not configured (disabled)'; }
        else                 { stateTxt = d.enable ? 'Active' : 'idle'; }
        const on = d.live && d.present && !absent;

        w.appendChild(labeled('State', el('span', {}, stateTxt)));
        w.appendChild(labeled('Enabled', el('span', {}, on ? (d.enable ? 'Yes' : 'No') : '-')));
        w.appendChild(labeled('Trigger', el('span', {}, on ? camioLbl('in_function', d.in_function) : '-')));
        w.appendChild(labeled('Strobe',  el('span', {}, on ? (d.strobe_enable ? 'On' : 'Off') : '-')));
        w.appendChild(labeled('Output',  el('span', {}, on ? camioLbl('out_source', d.out_source) : '-')));
        w.appendChild(labeled('Sync',    el('span', {}, on ? camioLbl('role', d.sync_role) : '-')));
        const lvl = (v, hi, lo) => (on && v === 1 ? hi : on && v === 0 ? lo : '-');
        w.appendChild(labeled('Sync lock', el('span', {}, lvl(d.st_locked, 'Locked', 'No lock'))));
        w.appendChild(labeled('Input pin', el('span', {}, lvl(d.st_optoin, 'High', 'Low'))));
    },
};

const PTP_PORT_STATES = ['?', 'Initializing', 'Faulty', 'Disabled', 'Listening',
                         'Pre-master', 'Master', 'Passive', 'Uncalibrated', 'Slave'];
const PTP_SYNC_INTERVALS = [[-3, '0.125 s'], [-2, '0.25 s'], [-1, '0.5 s'], [0, '1 s'], [1, '2 s']];

function ptpNs(v) {
    if (v === undefined || v === null) return '-';
    const a = Math.abs(v);
    if (a < 1000) return v + ' ns';
    if (a < 1e6) return (v / 1e3).toFixed(1) + ' \u00b5s';
    if (a < 1e9) return (v / 1e6).toFixed(2) + ' ms';
    return (v / 1e9).toFixed(3) + ' s';
}
function ptpDuration(s) {
    if (!s) return '-';
    const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), sec = s % 60;
    return (h ? h + ' h ' : '') + (h || m ? m + ' min ' : '') + sec + ' s';
}
function ptpAccuracy(code) {
    const t = { 0x20: '25 ns', 0x21: '100 ns', 0x22: '250 ns', 0x23: '1 \u00b5s', 0x24: '2.5 \u00b5s',
                0x25: '10 \u00b5s', 0x26: '25 \u00b5s', 0x27: '100 \u00b5s', 0x28: '250 \u00b5s', 0x29: '1 ms',
                0x2A: '2.5 ms', 0x2B: '10 ms', 0x2C: '25 ms', 0x2D: '100 ms', 0x2E: '250 ms',
                0x2F: '1 s', 0x30: '10 s', 0x31: '> 10 s', 0xFE: 'unknown' };
    return t[code] !== undefined ? (code === 0xFE ? 'unknown' : 'within ' + t[code]) : ('0x' + (code || 0).toString(16));
}
function ptpClockClass(c) {
    if (c === 6) return '6 (locked to a primary reference, e.g. GPS)';
    if (c === 7) return '7 (holdover from a primary reference)';
    if (c === 248) return '248 (default)';
    if (c === 255) return '255 (slave only)';
    return String(c);
}

const pagePtp = {
    help: {
        intro: 'PTP (IEEE 1588) synchronises the camera clock to a grandmaster on the network, so image timestamps from several cameras share one time base. GenICam applications switch it with PtpEnable; this page shows the clock state and holds the settings GenICam has no standard name for. PTP mode is not saved: it is off after every reboot.',
        tips: [
            'The network needs a PTP grandmaster (a PC running ptp4l, a PTP switch, a GPS clock), or cameras with role Auto.',
            'All devices must be in the same network segment / VLAN and use the same domain and transport. A UDP device does not hear a Layer 2 device.',
            'UDP PTP uses multicast 224.0.1.129, ports 319 and 320, with TTL 1: it does not cross routers. Switches with IGMP snooping need an IGMP querier on the segment, or they drop it.',
            'PTP-aware switches (boundary or transparent clocks) give the best accuracy; an ordinary switch adds its queueing delay as noise.',
            'Locked means the PTP clock follows the master and image timestamps are PTP time. Locking: the PTP clock is locked and the image timestamp clock is still settling (a few seconds).',
            'Saving settings while PTP is on restarts PTP: the lock is lost for about 10 to 20 seconds.',
            'Role Auto lets the camera become master when no better clock is present (lowest priority, then lowest clock ID wins). Slave only is the default.',
        ],
    },
    refreshMs: 1000,
    async render() {
        const page = $('#page');
        page.innerHTML = '';
        let d = {};
        try { d = await apiGet('/api/ptp'); } catch (e) { toast('PTP status unavailable: ' + (e && e.message ? e.message : e), 'error'); }

        const W = 'width: 23rem;';
        const grp = (t) => el('div', { class: 'label', style: 'font-weight:600; margin:0.5rem 0 0.1rem;' }, t);
        const sel = (id, val, opts) => {
            const sEl = el('select', { class: 'input', id, style: 'width:11.5rem;' });
            opts.forEach(([v, t]) => sEl.appendChild(el('option',
                Object.assign({ value: String(v) }, v === val ? { selected: 'selected' } : {}), t)));
            return sEl;
        };
        const num = (id, val, min, max) => el('input', { class: 'input', id, type: 'number',
            min: String(min), max: String(max), step: '1', value: String(val === undefined ? 0 : val), style: 'width:6rem;' });
        const iv = (id) => parseInt(($('#' + id) || {}).value || '0', 10);

        const sw = el('input', Object.assign({ id: 'ptp_enable', type: 'checkbox', class: 'toggle' },
            d.enable ? { checked: 'checked' } : {},
            d.clock_kind ? {} : { disabled: 'disabled' }));
        sw.addEventListener('change', async () => {
            sw.disabled = true;
            try {
                await apiPost('/api/ptp', { enable: sw.checked });
                toast(sw.checked ? 'PTP mode on' : 'PTP mode off', 'ok');
            } catch (e) {
                sw.checked = !sw.checked;
                toast('PTP switch failed: ' + (e && e.message ? e.message : e), 'error');
            } finally { sw.disabled = false; this.refresh(); }
        });
        const hw = d.clock_kind === 1 ? 'PHY'
                 : d.clock_kind === 2 ? 'Zynq GEM'
                 : 'none';
        const modePanel = panel({ css: W },
            panelTitle('PTP Mode'),
            labeled('PTP mode', sw,
                { title: 'Switch PTP on or off until the next reboot. GenICam PtpEnable switches the same state.' }),
            labeled('State', el('span', { id: 'ptp_state' }, '-')),
            labeled('Timestamp hardware', el('span', {}, hw),
                { title: 'Which clock takes the PTP packet timestamps.' }),
            labeled('GigE Vision PTP', el('span', {}, d.supported ? 'available (PtpEnable)' : 'not available'),
                { title: 'GigE Vision capability bit 12. Needs the PHY timestamps, which handle UDP.' }),
            el('div', { style: 'color:#666; font-size:0.9em; margin-top:0.4rem;' },
                'Not saved: PTP is off after every reboot.'));

        const adv = [
            grp('Advanced'),
            labeled('Delay mode', sel('ptp_delay', d.delay_mechanism, [[0, 'E2E (request-response)'], [1, 'P2P (peer delay)']]),
                { title: 'E2E is the GigE Vision default. P2P needs every switch on the path to support peer delay.' }),
            labeled('Sync interval as master', sel('ptp_sync', d.log_sync_interval, PTP_SYNC_INTERVALS),
                { title: 'How often this camera sends Sync when it is master (role Auto only).' }),
            labeled('DSCP', num('ptp_dscp', d.dscp, 0, 63),
                { title: 'DSCP value of PTP packets (UDP transport), for networks with QoS. 0 = default.' }),
        ];
        const saveBtn = el('button', { class: 'button' }, icon('save'), 'Save');
        saveBtn.addEventListener('click', async () => {
            const body = {
                transport: iv('ptp_transport'), domain: iv('ptp_domain'),
                role: iv('ptp_role'), priority1: iv('ptp_priority'),
                delay_mechanism: iv('ptp_delay'), log_sync_interval: iv('ptp_sync'), dscp: iv('ptp_dscp'),
            };
            if (isNaN(body.domain) || body.domain < 0 || body.domain > 127) { toast('Domain must be 0 to 127', 'error'); return; }
            if (isNaN(body.priority1) || body.priority1 < 0 || body.priority1 > 255) { toast('Priority must be 0 to 255', 'error'); return; }
            if (isNaN(body.dscp) || body.dscp < 0 || body.dscp > 63) { toast('DSCP must be 0 to 63', 'error'); return; }
            saveBtn.disabled = true;
            try {
                await apiPost('/api/ptp', body);
                toast(($('#ptp_enable') || {}).checked ? 'PTP settings saved, PTP restarting' : 'PTP settings saved', 'ok');
            } catch (e) { toast('Save failed: ' + (e && e.message ? e.message : e), 'error'); }
            finally { saveBtn.disabled = false; }
        });
        const setPanel = panel({ css: W },
            panelTitle('Settings'),
            labeled('Transport', sel('ptp_transport', d.transport, [[0, 'UDP/IPv4'], [1, 'Layer 2']]),
                { title: 'UDP/IPv4 is the GigE Vision default. Every device in the PTP network must use the same transport.' }),
            labeled('Domain', num('ptp_domain', d.domain, 0, 127),
                { title: 'PTP domain number, 0 to 127. Devices only synchronise within one domain.' }),
            grp('Master election'),
            labeled('Role', sel('ptp_role', d.role, [[0, 'Slave only'], [1, 'Auto']]),
                { title: 'Slave only: never becomes master. Auto: can become master when no better clock is present.' }),
            labeled('Priority', num('ptp_priority', d.priority1, 0, 255),
                { title: 'priority1 for the election (role Auto): lower wins. Default 128.' }),
            ...adv,
            el('div', { style: 'color:#666; font-size:0.9em; margin-top:0.4rem;' },
                'Saving while PTP is on restarts it: the lock is lost for about 10 to 20 s.'),
            el('div', { class: 'labeled', style: 'justify-content:end; margin-top:0.5rem;' }, saveBtn));

        const statusPanel = panel({ css: W }, panelTitle('Clock Status'), el('div', { id: 'ptp_live' }));
        const diagPanel = panel({ css: W }, panelTitle('Diagnostics'), el('div', { id: 'ptp_diag' }));

        page.appendChild(el('div', { class: 'container ptp-page', style: 'gap:0.6rem; align-items:flex-start; flex-wrap:wrap;' },
            setPanel, statusPanel, modePanel, diagPanel));
        this._paintLive(d);
    },
    async refresh() {
        try { this._paintLive(await apiGet('/api/ptp')); } catch (_) {}
    },
    _paintLive(d) {
        d = d || {};
        const st = $('#ptp_state');
        let state;
        if (d.apply_state === 1)        state = d.enabled ? 'Applying (starting PTP)' : 'Applying (stopping PTP)';
        else if (!d.enabled)            state = 'Off';
        else if (!d.running)            state = 'Starting (ptp4l not answering yet)';
        else if (d.servo === 1)         state = 'Locked (' + (PTP_PORT_STATES[d.port_state] || '?') + ')';
        else if (d.servo === 2)         state = 'Locking (image clock settling)';
        else                            state = 'Not locked (' + (PTP_PORT_STATES[d.port_state] || '?') + ')';
        if (d.apply_state === 2)        state += ' - last change failed, see System Log';
        if (st) st.textContent = state;
        const sw = $('#ptp_enable');
        if (sw && !sw.disabled && document.activeElement !== sw) sw.checked = !!d.enabled;

        const w = $('#ptp_live');
        if (w) {
            w.innerHTML = '';
            const on = d.enabled && d.running;
            const row = (l, v, t) => w.appendChild(labeled(l, el('span', {}, v), t ? { title: t } : undefined));
            row('Port state', d.enabled ? (PTP_PORT_STATES[d.port_state] || '?') : 'Disabled');
            row('PTP clock', on ? (d.port_state === 9 || d.port_state === 6 ? 'locked' : 'not locked') : '-',
                'The PTP hardware clock that ptp4l steers.');
            row('Image timestamp clock', !on ? '-' : d.follow_state < 0 ? 'follows the PTP clock'
                : d.follow_state === 2 ? 'locked (' + ptpNs(d.follow_offset_ns) + ')' : 'settling',
                'The clock that stamps GigE frames, kept on the PTP clock by the driver.');
            const slave = on && d.port_state === 9;
            row('Offset from master', slave ? ptpNs(d.offset_ns) : '-');
            row('Mean path delay', slave ? ptpNs(d.mean_path_delay_ns) : '-');
            row('Grandmaster', on ? d.gm_id : '-');
            row('GM class / accuracy', on ? ptpClockClass(d.gm_clock_class) + ', ' + ptpAccuracy(d.gm_clock_accuracy) : '-');
            row('Parent', on ? d.parent_id + ' port ' + d.parent_port : '-');
            row('This clock', on ? d.clock_id : '-');
            let t = '-', off = '-';
            if (on && d.ptp_time_sec) {
                const utcOff = d.utc_offset_valid ? d.utc_offset : 37;
                const utc = new Date((d.ptp_time_sec - utcOff) * 1000);
                t = utc.toISOString().replace('T', ' ').replace(/\.\d+Z$/, ' UTC');
                off = utcOff + ' s' + (d.utc_offset_valid ? '' : ' (assumed)');
            }
            row('PTP time', t, 'PTP time (TAI) shown as UTC.');
            row('UTC offset', off, 'TAI minus UTC. "assumed": the grandmaster does not send a valid offset, so 37 s is used.');
            row('Locked for', on ? ptpDuration(d.locked_for_s) : '-');
            row('Lock losses', d.lock_losses !== undefined ? String(d.lock_losses) : '-', 'Since boot.');
        }

        const g = $('#ptp_diag');
        if (g) {
            g.innerHTML = '';
            const row = (l, v) => g.appendChild(labeled(l, el('span', {}, v)));
            row('PTP mode on for', d.enabled ? ptpDuration(d.enabled_for_s) : '-');
            row('Last ptp4l answer', d.enabled && d.running ? d.sample_age_ms + ' ms ago' : '-');
            row('Transport in use', d.transport_in_use === 1 ? 'Layer 2' : 'UDP/IPv4');
            row('Steps removed', String(d.steps_removed || 0));
            row('Received Sync / Follow_Up', (d.rx_sync || 0) + ' / ' + (d.rx_follow_up || 0));
            row('Received Announce', String(d.rx_announce || 0));
            row('Delay_Req sent / Resp', (d.tx_delay_req || 0) + ' / ' + (d.rx_delay_resp || 0));
            row('Pdelay_Req sent / Resp', (d.tx_pdelay_req || 0) + ' / ' + (d.rx_pdelay_resp || 0));
            row('Sync / Announce sent', (d.tx_sync || 0) + ' / ' + (d.tx_announce || 0));
            row('Own clock class', ptpClockClass(d.clock_class));
        }
    },
};

const PAGES = {
    dashboard:       pageDashboard,
    isp:             pageIsp,
    hdmi:            pageHdmi,
    iosync:          pageIosync,
    ptp:             pagePtp,
    network:         pageNetwork,
    firmware_update: pageFirmwareUpdate,
    usb_fw:          pageUsbFw,
    bitstream:       pageBitstream,
    sensor:          pageSensor,
    log:             pageLog,
    admin_password:  pageAdminPassword,
};

async function navigate() {
    const id = (location.hash || '#dashboard').slice(1);
    const page = PAGES[id] || pageDashboard;
    activePage = page;
    stopPolling();

    for (const n of NAV) {
        const e = document.getElementById('nav_' + n.id);
        if (e) e.classList.toggle('sidebar-selected-item', n.id === id);
        if (e && n.id === id) setText('tb_title', n.title);
    }
    try {
        await page.render();
        if (page.help) $('#page').appendChild(helpPanel(page.help));
        startPolling(page);
    } catch (e) {
        toast('Page error: ' + e.message, 'error');
    }
}

window.addEventListener('hashchange', navigate);
window.addEventListener('DOMContentLoaded', async () => {
    buildShell();
    if (!authToken) {
        showLogin();
    } else {
        try { await apiGet('/api/usb_firmware'); navigate(); startWS(); }
        catch (_) {  }
    }
});
