// SPDX-License-Identifier: GPL-3.0-or-later
// UI-only adaptation of ESPHome web_server v3. Leave /update and its auth/guard
// intact: web_server.ota:false would disable our settings-page LAN upload too.
// Recheck the app/table rendering hooks when upgrading ESPHome/the CDN frontend.
(() => {
  const splitDiagnostics = (app, Table) => {
    const table = Table?.prototype;
    if (typeof app.prototype.renderLog !== 'function' || typeof app.prototype.updated !== 'function' ||
        typeof table?._groupEntities !== 'function' || typeof table.renderShowAll !== 'function' ||
        typeof table.updated !== 'function') {
      console.warn('OpenGarage: diagnostics layout hook unavailable; retaining the original grouping.');
      return;
    }
    const views = new WeakMap();
    // ESPHome's generic cover renderer always includes Stop, even though our
    // cover explicitly has supports_stop=false. Keep only supported commands.
    if (typeof table.control === 'function') {
      const originalControl = table.control, controls = new WeakMap();
      table.control = function (entity) {
        const door = entity.domain === 'cover' && entity.name === 'Garage Door';
        const remoteLock = entity.domain === 'lock' && entity.name === 'Remote Lock';
        if (!door && !remoteLock && entity.domain !== 'button') return originalControl.call(this, entity);
        if (!controls.has(entity)) {
          const row = document.createElement('span'), buttons = [];
          row.style.display = 'inline-flex'; row.style.gap = '.4rem'; row.style.flexWrap = 'wrap';
          // Remote Lock disables RF remotes; it has no latch-opening action.
          const actions = door ? [['↑ Open', 'open', 'OPEN'], ['↓ Close', 'close', 'CLOSED']] :
            remoteLock ? [['Lock', 'lock', 'LOCKED'], ['Unlock', 'unlock', 'UNLOCKED']] : [['Press', 'press', null]];
          for (const [label, action, state] of actions) {
            const button = document.createElement('button'); button.type = 'button'; button.textContent = label;
            button.title = door ? (action === 'open' ? 'Open garage door' : 'Close garage door') :
              remoteLock ? (action === 'lock' ? 'Disable RF remotes' : 'Enable RF remotes') : 'Press ' + entity.name;
            button.setAttribute('aria-label', button.title);
            button.style.font = 'inherit'; button.style.minHeight = '44px'; button.style.padding = '.5rem .75rem';
            button.addEventListener('click', () => { if (!button.disabled) this.restAction(entity, action); });
            row.append(button); buttons.push({button, state});
          }
          controls.set(entity, {row, buttons});
        }
        const view = controls.get(entity);
        for (const {button, state} of view.buttons) {
          // Preserve ESPHome's assumed-state behavior: both directions remain
          // available; the firmware validates the real door state on request.
          button.disabled = (remoteLock || (door && entity.assumed_state !== true)) && entity.state === state;
          button.className = button.disabled ? 'abuttonIsState' : 'abutton';
          if (remoteLock) {
            // Color indicates reported status, never merely the available action.
            button.style.background = button.disabled ? (state === 'LOCKED' ? '#b3261e' : '#237a3b') : '#e5e7eb';
            button.style.color = button.disabled ? '#fff' : '#263238';
            button.setAttribute('aria-pressed', String(button.disabled));
          }
        }
        return view.row;
      };
    }
    const belongs = (view, entity) => {
      // Presentation only: the HA entity and authenticated REST action remain.
      // Keep this exact domain/name paired with the shared YAML entity name.
      if (entity.domain === 'button' && entity.name === 'Enter Firmware Update Mode') return false;
      const diagnostic = Number(entity.entity_category) === 2;
      return view._ogDiagnosticsOnly ? diagnostic : !view._ogDiagnosticsTarget || !diagnostic;
    };
    const group = table._groupEntities, showAll = table.renderShowAll, tableUpdated = table.updated;
    const preferred = new Map(['Garage Door', 'Cancel Pending Action', 'Request Door Toggle', 'Door Open',
      'Door State', 'Distance', 'Vehicle Present', 'Vehicle State'].map((name, index) => [name, index]));
    table._groupEntities = function (entities) {
      const groups = group.call(this, entities.filter(entity => belongs(this, entity)));
      return new Map(Array.from(groups, ([name, rows]) => {
        const configuration = name === 'Configuration' && rows.every(entity => Number(entity.entity_category) === 1);
        // Sort a copy, retaining shared entity/history objects and stable order
        // for unlisted rows. Missing/late-discovered entities need no special case.
        if (name === 'Sensor and Control') rows = rows.slice().sort((a, b) =>
          (preferred.get(a.name) ?? preferred.size) - (preferred.get(b.name) ?? preferred.size));
        if (configuration) rows = rows.slice().sort((a, b) =>
          Number(a.domain === 'button' && a.name === 'Restart Device') - Number(b.domain === 'button' && b.name === 'Restart Device'));
        return [configuration ? 'Garage Configuration' : name, rows];
      }));
    };
    table.renderShowAll = function () {
      return this.entities.some(entity => belongs(this, entity) && entity.is_disabled_by_default) ? showAll.call(this) : null;
    };
    const sync = (source, target) => {
      // Share the existing entity/history objects. Only the original table owns
      // state/detail subscriptions; the right table is a presentation mirror.
      target.entities = source.entities;
      target.groups = source.groups;
      target.has_controls = source.has_controls;
      target.requestUpdate();
    };
    table.updated = function (changes) {
      tableUpdated.call(this, changes);
      if (this._ogDiagnosticsTarget) sync(this, this._ogDiagnosticsTarget);
    };
    class DiagnosticsTable extends Table {
      constructor() { super(); this._ogDiagnosticsOnly = true; }
      connectedCallback() {
        super.connectedCallback();
        window.source?.removeEventListener('state', this._handleState);
        window.source?.removeEventListener('sorting_group', this._handleSortingGroup);
      }
    }
    customElements.define('og-diagnostics-table', DiagnosticsTable);
    app.prototype.renderLog = function () {
      if (!views.has(this)) {
        const column = document.createElement('section');
        column.id = 'col_logs'; column.className = 'col';
        const diagnostics = document.createElement('og-diagnostics-table');
        const log = document.createElement('esp-log'); log.setAttribute('rows', '50');
        // Expanding a diagnostic header should expand its own (right) column.
        diagnostics.addEventListener('entity-tab-header-double-clicked', event => {
          event.stopPropagation();
          diagnostics.dispatchEvent(new CustomEvent('log-tab-header-double-clicked', {bubbles: true, composed: true}));
        });
        column.append(diagnostics);
        views.set(this, {column, diagnostics, log});
      }
      const view = views.get(this);
      view.diagnostics.scheme = this.scheme; view.log.scheme = this.scheme;
      if (this.config.log) {
        if (view.log.parentNode !== view.column) view.column.append(view.log);
      } else { view.log.remove(); }
      return view.column;
    };
    const appUpdated = app.prototype.updated;
    app.prototype.updated = function (changes) {
      appUpdated.call(this, changes);
      const source = this.shadowRoot?.querySelector('esp-entity-table'), view = views.get(this);
      if (!source || !view) return;
      if (source._ogDiagnosticsTarget !== view.diagnostics) {
        source._ogDiagnosticsTarget = view.diagnostics;
        source.requestUpdate();
      }
      sync(source, view.diagnostics);
    };
  };
  const makePanel = () => {
    const panel = document.createElement('div');
    const heading = document.createElement('div');
    heading.className = 'tab-header';
    heading.textContent = 'Device Setup & Updates';
    const body = document.createElement('div');
    body.className = 'tab-container';
    const link = document.createElement('a');
    link.href = '/og/setup';
    link.textContent = 'Open setup';
    link.setAttribute('aria-label', 'Open Device Setup & Updates');
    link.style.display = 'inline-block';
    link.style.padding = '.65rem 1rem'; link.style.margin = '.5rem .75rem';
    link.style.background = '#176c86'; link.style.color = '#fff';
    link.style.borderRadius = '.35rem'; link.style.textDecoration = 'none'; link.style.fontWeight = '600';
    body.append(link);
    panel.append(heading, body);
    return panel;
  };
  // Keep settings reachable if a future upstream frontend removes the hook.
  // This fallback is not a claim that an unknown frontend's uploader is removed.
  const fallback = () => {
    if (document.getElementById('og-settings-fallback')) return;
    const panel = makePanel();
    panel.id = 'og-settings-fallback';
    document.body.prepend(panel);
    console.warn('OpenGarage: dashboard layout changed; use Device Setup & Updates.');
  };
  const install = async () => {
    await customElements.whenDefined('esp-app');
    const app = customElements.get('esp-app');
    if (typeof app?.prototype.renderOta !== 'function') { fallback(); return; }
    const panels = new WeakMap();
    app.prototype.renderOta = function () {
      if (!panels.has(this)) panels.set(this, makePanel());
      return panels.get(this);
    };
    await customElements.whenDefined('esp-entity-table');
    splitDiagnostics(app, customElements.get('esp-entity-table'));
    // Handles both module-before-app and app-before-module load orders. Lit
    // owns the replacement DOM, so status updates do not resurrect the form.
    document.querySelectorAll('esp-app').forEach(instance => instance.requestUpdate());
  };
  install().catch(fallback);
})();
