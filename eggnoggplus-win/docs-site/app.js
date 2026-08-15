(() => {
  "use strict";

  const pageGroups = [
    {
      label: "Start",
      pages: [
        ["home", "Overview", "index.html", "Documentation map and framework scope."],
        ["installation", "Install", "getting-started/installation.html", "Install the proxy DLL and verify first launch."],
        ["first-launch", "First launch", "getting-started/first-launch.html", "Confirm the framework loaded correctly."],
        ["troubleshooting", "Troubleshooting", "getting-started/troubleshooting.html", "Diagnose launch, DLL, mod, and online failures."]
      ]
    },
    {
      label: "Learn",
      pages: [
        ["guide-modding", "Build a Lua mod", "guides/modding.html", "Author and package an ordinary framework mod."],
        ["guide-maps", "Build a map", "guides/maps.html", "Create V1 and V2 map packages and behavior scripts."],
        ["guide-online", "Online multiplayer", "guides/online.html", "Configure, test, and diagnose rollback multiplayer."],
        ["guide-discord", "Discord integrations", "guides/discord.html", "Deploy Rich Presence and LFG integrations."],
        ["mods", "Using mods", "getting-started/mods.html", "Install, configure, enable, and reload mods."],
        ["configuration", "Configuration basics", "getting-started/configuration.html", "Find and edit the correct settings file."],
        ["console", "Console basics", "getting-started/console.html", "Use the developer console and copy diagnostics."]
      ]
    },
    {
      label: "Framework API",
      pages: [
        ["api-index", "API overview", "api/index.html", "Choose an API area and understand shared conventions."],
        ["api-runtime", "Runtime & lifecycle", "api/runtime.html", "mod, config, storage, interop, input, and os."],
        ["api-ui", "UI & rendering", "api/ui.html", "ui, assets, font, texture, and anim."],
        ["api-game", "Gameplay & online", "api/game.html", "game telemetry, mutation, bot, and online bridges."],
        ["api-audio", "Audio", "api/audio.html", "Sound, music, bytebeat, and generated-audio control."],
        ["api-content", "Content registry", "api/content.html", "Transactional symbolic tile definitions."],
        ["api-services", "Files & networking", "api/services.html", "File pickers, TCP slots, and asynchronous HTTP."],
        ["api-map-lua", "Map Lua", "api/map-lua.html", "Deterministic map callbacks, objects, tiles, and state."]
      ]
    },
    {
      label: "Reference",
      pages: [
        ["reference-map-format", "Map package format", "reference/map-format.html", "Complete V1/V2 schema, glyph, tileset, and limit reference."],
        ["reference-configuration", "Configuration files", "reference/configuration.html", "Every framework, mod, online, updater, and music setting."],
        ["reference-console", "Console commands", "reference/console.html", "Complete command, argument, alias, and safety reference."],
        ["reference-audio", "Audio formats", "reference/audio.html", "Playlist, bytebeat, floatbeat, funcbeat, and Dollchan formats."],
        ["reference-security", "Security model", "reference/security.html", "Trust boundaries, credentials, networking, deep links, and updates."]
      ]
    },
    {
      label: "Maintain",
      pages: [
        ["architecture", "Architecture", "framework/architecture.html", "How the proxy, hooks, Lua runtime, and services fit together."],
        ["file-layout", "Source layout", "framework/file-layout.html", "Repository and installed-runtime file map."],
        ["building", "Build from source", "framework/building.html", "Compile safely with the supported toolchain."],
        ["guide-testing", "Test & release", "guides/testing-releasing.html", "Guarded suites, manual acceptance, and release gates."],
        ["guide-server", "Deploy the server", "guides/server-deployment.html", "Update the online service without replacing state."],
        ["guide-updater", "Publish updates", "guides/updater-release.html", "Build manifests, publish releases, and recover transactions."]
      ]
    }
  ];

  const appScript = document.querySelector('script[src$="app.js"]');
  const root = document.body.dataset.root ||
    (appScript?.getAttribute("src") || "").replace(/app\.js(?:[?#].*)?$/, "");
  const pageId = document.body.dataset.page || inferPageId();
  const reduceMotion = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  let apiLoadPromise = null;
  let searchItems = [];

  function inferPageId() {
    const normalized = window.location.pathname.replaceAll("\\", "/");
    const match = pageGroups.flatMap(group => group.pages)
      .find(([, , path]) => normalized.endsWith(path));
    return match?.[0] || (normalized.endsWith("/") ? "home" : "");
  }

  function escapeHtml(value) {
    return String(value ?? "")
      .replaceAll("&", "&amp;")
      .replaceAll("<", "&lt;")
      .replaceAll(">", "&gt;")
      .replaceAll('"', "&quot;");
  }

  function slug(value) {
    return String(value)
      .toLowerCase()
      .replace(/[^a-z0-9_.:-]+/g, "-")
      .replace(/^-+|-+$/g, "");
  }

  function buildShell() {
    const oldMain = document.querySelector("main");
    if (!oldMain) return;

    oldMain.remove();
    oldMain.id = oldMain.id || "main";
    oldMain.classList.add("doc-main");

    const skip = document.createElement("a");
    skip.className = "skip-link";
    skip.href = "#main";
    skip.textContent = "Skip to content";

    const header = document.createElement("header");
    header.className = "site-header";
    header.innerHTML = `
      <div class="header-inner">
        <button class="nav-toggle" id="nav-toggle" type="button"
                aria-label="Open documentation navigation" aria-controls="global-nav"
                aria-expanded="false"><span></span><span></span><span></span></button>
        <a class="wordmark" href="${root}index.html" aria-label="Eggnogg+ documentation home">
          <span class="wordmark-mark">+</span>
          <span class="wordmark-copy"><b>EGGNOGG+</b><small>documentation</small></span>
        </a>
        <button class="search-trigger" id="search-trigger" type="button"
                aria-haspopup="dialog" aria-controls="search-dialog">
          <span>Search documentation</span><kbd>/</kbd>
        </button>
        <a class="header-api-link" href="${root}api/index.html">API index <span aria-hidden="true">→</span></a>
      </div>`;

    const nav = document.createElement("aside");
    nav.className = "global-nav";
    nav.id = "global-nav";
    nav.setAttribute("aria-label", "Documentation");
    nav.innerHTML = `
      <div class="nav-scroll">
        <div class="nav-intro">
          <span>Contents</span>
          <p>Instructions, API documentation, and reference pages.</p>
        </div>
        ${pageGroups.map(group => `
          <section class="nav-group">
            <h2>${escapeHtml(group.label)}</h2>
            <ul>${group.pages.map(([id, title, path]) => `
              <li><a href="${root}${path}"${id === pageId ? ' aria-current="page"' : ""}>${escapeHtml(title)}</a></li>
            `).join("")}</ul>
          </section>`).join("")}
      </div>`;

    const toc = document.createElement("aside");
    toc.className = "page-toc";
    toc.id = "page-toc";
    toc.setAttribute("aria-label", "On this page");

    const shell = document.createElement("div");
    shell.className = "doc-shell";
    shell.append(nav, oldMain, toc);

    const backdrop = document.createElement("button");
    backdrop.className = "nav-backdrop";
    backdrop.type = "button";
    backdrop.id = "nav-backdrop";
    backdrop.setAttribute("aria-label", "Close navigation");

    const footer = document.createElement("footer");
    footer.className = "site-footer";
    footer.innerHTML = `<p><b>EGGNOGG+</b> documentation</p>
      <span><a href="${root}reference/security.html">Security reference</a><a href="${root}framework/file-layout.html">File layout</a></span>`;

    const search = document.createElement("dialog");
    search.className = "search-dialog";
    search.id = "search-dialog";
    search.setAttribute("aria-labelledby", "search-title");
    search.innerHTML = `
      <div class="search-head">
        <div>
          <p class="eyebrow">Documentation search</p>
          <h2 id="search-title">Find a page or API function</h2>
        </div>
        <button class="dialog-close" type="button" data-close-search aria-label="Close search">×</button>
      </div>
      <label class="search-field">
        <span class="sr-only">Search</span>
        <input id="doc-search" type="search" autocomplete="off"
               placeholder="Try mod.on_tick, custom tiles, or rollback…" autofocus>
      </label>
      <div class="search-help"><span>Navigate <kbd>↑</kbd><kbd>↓</kbd></span><span>Open <kbd>Enter</kbd></span><span>Close <kbd>Esc</kbd></span></div>
      <div class="search-results" id="search-results" role="listbox"></div>`;

    document.body.replaceChildren(skip, header, shell, backdrop, footer, search);
    document.body.classList.toggle("home-page", pageId === "home");
    bindShellEvents();
  }

  function bindShellEvents() {
    const toggle = document.querySelector("#nav-toggle");
    const backdrop = document.querySelector("#nav-backdrop");
    const nav = document.querySelector("#global-nav");
    const closeNav = () => {
      document.body.classList.remove("nav-open");
      toggle?.setAttribute("aria-expanded", "false");
    };
    toggle?.addEventListener("click", () => {
      const open = !document.body.classList.contains("nav-open");
      document.body.classList.toggle("nav-open", open);
      toggle.setAttribute("aria-expanded", String(open));
    });
    backdrop?.addEventListener("click", closeNav);
    nav?.addEventListener("click", event => {
      if (event.target.closest("a")) closeNav();
    });

    const dialog = document.querySelector("#search-dialog");
    const input = document.querySelector("#doc-search");
    const openSearch = () => {
      if (!dialog) return;
      buildSearchIndex().then(() => {
        if (!dialog.open) dialog.showModal();
        window.setTimeout(() => input?.focus(), 0);
        renderSearch("");
      });
    };
    document.querySelector("#search-trigger")?.addEventListener("click", openSearch);
    document.querySelector("[data-close-search]")?.addEventListener("click", () => dialog?.close());
    dialog?.addEventListener("click", event => {
      if (event.target === dialog) dialog.close();
    });
    input?.addEventListener("input", () => renderSearch(input.value));
    input?.addEventListener("keydown", handleSearchKeys);
    document.addEventListener("keydown", event => {
      if (event.key === "/" && !/input|textarea|select/i.test(document.activeElement?.tagName || "")) {
        event.preventDefault();
        openSearch();
      }
    });
  }

  function loadApiData() {
    if (window.EGGNOGG_API_GROUPS) return Promise.resolve(window.EGGNOGG_API_GROUPS);
    if (apiLoadPromise) return apiLoadPromise;
    apiLoadPromise = new Promise((resolve, reject) => {
      const script = document.createElement("script");
      script.src = `${root}api-data.js`;
      script.onload = () => resolve(window.EGGNOGG_API_GROUPS || []);
      script.onerror = reject;
      document.head.append(script);
    });
    return apiLoadPromise;
  }

  async function buildSearchIndex() {
    const pageItems = pageGroups.flatMap(group =>
      group.pages.map(([id, title, path, description]) => ({
        id,
        title,
        href: `${root}${path}`,
        description,
        category: group.label,
        terms: `${title} ${description} ${group.label}`.toLowerCase()
      }))
    );
    let apiItems = [];
    try {
      const groups = await loadApiData();
      apiItems = groups.flatMap(group => group.entries.map(entry => ({
        id: `${group.id}-${entry.id}`,
        title: entry.name,
        href: `${root}${group.page || "api/index.html"}#${entry.id}`,
        description: entry.summary,
        category: `${group.label} API`,
        terms: `${entry.name} ${entry.signature} ${entry.summary} ${(entry.notes || []).join(" ")} ${group.label}`.toLowerCase()
      })));
    } catch {
      apiItems = [];
    }
    searchItems = [...apiItems, ...pageItems];
  }

  function scoreItem(item, terms) {
    const title = item.title.toLowerCase();
    let score = 0;
    for (const term of terms) {
      if (!item.terms.includes(term)) return -1;
      if (title === term) score += 100;
      else if (title.startsWith(term)) score += 45;
      else if (title.includes(term)) score += 25;
      else score += 5;
    }
    if (item.category.includes("API")) score += 4;
    return score;
  }

  function renderSearch(rawQuery) {
    const host = document.querySelector("#search-results");
    if (!host) return;
    const terms = rawQuery.trim().toLowerCase().split(/\s+/).filter(Boolean);
    const items = terms.length
      ? searchItems.map(item => [item, scoreItem(item, terms)])
        .filter(([, score]) => score >= 0)
        .sort((a, b) => b[1] - a[1])
        .slice(0, 16)
        .map(([item]) => item)
      : searchItems.filter(item => item.category.includes("API")).slice(0, 8);
    host.innerHTML = items.length ? items.map((item, index) => `
      <a class="search-result${index === 0 ? " is-active" : ""}" role="option"
         aria-selected="${index === 0}" href="${item.href}">
        <span class="search-result-type">${escapeHtml(item.category)}</span>
        <strong>${escapeHtml(item.title)}</strong>
        <span>${escapeHtml(item.description)}</span>
      </a>`).join("") : `<p class="search-empty">No results. Try a namespace, function name, command, or file.</p>`;
  }

  function handleSearchKeys(event) {
    const results = [...document.querySelectorAll(".search-result")];
    if (!results.length) return;
    const current = Math.max(0, results.findIndex(node => node.classList.contains("is-active")));
    let next = current;
    if (event.key === "ArrowDown") next = Math.min(results.length - 1, current + 1);
    else if (event.key === "ArrowUp") next = Math.max(0, current - 1);
    else if (event.key === "Enter") {
      event.preventDefault();
      results[current]?.click();
      return;
    } else return;
    event.preventDefault();
    results.forEach((node, index) => {
      node.classList.toggle("is-active", index === next);
      node.setAttribute("aria-selected", String(index === next));
    });
    results[next]?.scrollIntoView({ block: "nearest" });
  }

  function renderApiReference() {
    const host = document.querySelector("#api-browser");
    if (!host) return Promise.resolve();
    return loadApiData().then(groups => {
      const requested = (host.dataset.groups || "*").split(",").map(value => value.trim());
      const selected = requested.includes("*") ? groups : groups.filter(group => requested.includes(group.id));
      host.innerHTML = `
        <div class="api-tools" role="search">
          <label class="api-filter">
            <span>Filter this reference</span>
            <input id="api-filter" type="search" placeholder="Function, parameter, or behavior…" autocomplete="off">
          </label>
          <div class="api-filter-status" id="api-filter-status" aria-live="polite"></div>
        </div>
        <nav class="namespace-strip" aria-label="API namespaces">
          ${selected.map(group => `<a href="#namespace-${slug(group.id)}"><code>${escapeHtml(group.label)}</code><span>${group.entries.length}</span></a>`).join("")}
        </nav>
        <div class="api-groups">
          ${selected.map(renderApiGroup).join("")}
        </div>`;
      bindApiFilter();
      if (window.location.hash) revealHashTarget();
    });
  }

  function renderApiGroup(group) {
    return `
      <section class="api-namespace" id="namespace-${slug(group.id)}" data-api-group="${escapeHtml(group.id)}">
        <header class="namespace-header">
          <p class="eyebrow">${escapeHtml(group.kind || "Lua namespace")}</p>
          <div class="namespace-title-row">
            <h2><code>${escapeHtml(group.label)}</code></h2>
            <span>${group.entries.length} ${group.entries.length === 1 ? "member" : "members"}</span>
          </div>
          <p>${escapeHtml(group.description)}</p>
          ${group.lifecycle ? `<div class="namespace-lifecycle"><strong>Lifecycle:</strong> ${escapeHtml(group.lifecycle)}</div>` : ""}
        </header>
        <div class="api-entry-list">
          ${group.entries.map(entry => renderApiEntry(group, entry)).join("")}
        </div>
      </section>`;
  }

  function renderApiEntry(group, entry) {
    const params = entry.params || [];
    const tags = entry.tags || [];
    return `
      <article class="api-entry" id="${escapeHtml(entry.id)}"
               data-search="${escapeHtml(`${entry.name} ${entry.signature} ${entry.summary} ${params.map(p => p.join(" ")).join(" ")} ${(entry.notes || []).join(" ")}`.toLowerCase())}">
        <header class="api-entry-header">
          <div class="api-entry-name">
            <a class="anchor-link" href="#${escapeHtml(entry.id)}" aria-label="Link to ${escapeHtml(entry.name)}">#</a>
            <h3><code>${escapeHtml(entry.name)}</code></h3>
          </div>
          <div class="api-tags">
            <span class="api-kind">${escapeHtml(entry.kind || "function")}</span>
            ${tags.map(tag => `<span class="api-tag api-tag-${slug(tag)}">${escapeHtml(tag)}</span>`).join("")}
          </div>
        </header>
        <pre class="signature"><code>${escapeHtml(entry.signature)}</code></pre>
        <p class="api-summary">${escapeHtml(entry.summary)}</p>
        ${entry.details ? `<p class="api-details">${escapeHtml(entry.details)}</p>` : ""}
        <div class="contract-grid">
          <section>
            <h4>Parameters</h4>
            ${params.length ? `
              <div class="parameter-list">
                ${params.map(([name, type, description, requirement]) => `
                  <div class="parameter">
                    <div><code>${escapeHtml(name)}</code><span>${escapeHtml(type)}</span>${requirement ? `<em>${escapeHtml(requirement)}</em>` : ""}</div>
                    <p>${escapeHtml(description)}</p>
                  </div>`).join("")}
              </div>` : `<p class="empty-contract">No parameters.</p>`}
          </section>
          <section>
            <h4>Returns</h4>
            <p>${escapeHtml(entry.returns || "No values.")}</p>
            ${entry.errors ? `<h4>Failure</h4><p>${escapeHtml(entry.errors)}</p>` : ""}
          </section>
        </div>
        ${(entry.notes || []).length ? `
          <aside class="api-notes">
            <h4>Behavior and constraints</h4>
            <ul>${entry.notes.map(note => `<li>${escapeHtml(note)}</li>`).join("")}</ul>
          </aside>` : ""}
        ${entry.example ? `
          <section class="api-example">
            <div><h4>Example</h4>${entry.exampleTitle ? `<span>${escapeHtml(entry.exampleTitle)}</span>` : ""}</div>
            <pre><code>${escapeHtml(entry.example)}</code></pre>
          </section>` : ""}
      </article>`;
  }

  function bindApiFilter() {
    const input = document.querySelector("#api-filter");
    const status = document.querySelector("#api-filter-status");
    if (!input) return;
    const update = () => {
      const query = input.value.trim().toLowerCase();
      const terms = query.split(/\s+/).filter(Boolean);
      let visible = 0;
      document.querySelectorAll(".api-namespace").forEach(group => {
        let groupVisible = 0;
        group.querySelectorAll(".api-entry").forEach(entry => {
          const match = terms.every(term => entry.dataset.search.includes(term));
          entry.hidden = !match;
          if (match) {
            visible += 1;
            groupVisible += 1;
          }
        });
        group.hidden = groupVisible === 0;
      });
      if (status) status.textContent = query ? `${visible} matching API members` : "";
    };
    input.addEventListener("input", update);
  }

  function addCopyButtons() {
    document.querySelectorAll("pre").forEach(pre => {
      if (pre.querySelector(".copy-button")) return;
      const code = pre.querySelector("code");
      if (!code) return;
      const button = document.createElement("button");
      button.className = "copy-button";
      button.type = "button";
      button.textContent = "Copy";
      button.setAttribute("aria-label", "Copy code");
      button.addEventListener("click", async () => {
        try {
          await navigator.clipboard.writeText(code.textContent);
          button.textContent = "Copied";
        } catch {
          button.textContent = "Select";
        }
        window.setTimeout(() => { button.textContent = "Copy"; }, 1400);
      });
      pre.append(button);
    });
  }

  function buildToc() {
    const toc = document.querySelector("#page-toc");
    const main = document.querySelector("#main");
    if (!toc || !main) return;
    const headings = [...main.querySelectorAll("h2, h3")]
      .filter(node => !node.closest(".api-entry") && !node.closest(".api-tools"));
    if (headings.length < 2) {
      toc.hidden = true;
      return;
    }
    headings.forEach(node => {
      if (!node.id) node.id = slug(node.textContent);
    });
    toc.innerHTML = `<div class="toc-inner"><h2>On this page</h2><ol>
      ${headings.map(node => `<li class="toc-${node.tagName.toLowerCase()}"><a href="#${node.id}">${escapeHtml(node.textContent)}</a></li>`).join("")}
    </ol></div>`;

    if ("IntersectionObserver" in window) {
      const links = new Map([...toc.querySelectorAll("a")].map(link => [link.hash.slice(1), link]));
      const observer = new IntersectionObserver(entries => {
        entries.filter(entry => entry.isIntersecting).forEach(entry => {
          links.forEach(link => link.removeAttribute("aria-current"));
          links.get(entry.target.id)?.setAttribute("aria-current", "location");
        });
      }, { rootMargin: "-20% 0px -70% 0px" });
      headings.forEach(heading => observer.observe(heading));
    }
  }

  function revealHashTarget() {
    const target = document.querySelector(window.location.hash);
    if (!target) return;
    target.hidden = false;
    target.closest(".api-namespace")?.removeAttribute("hidden");
    target.classList.add("hash-target");
    if (!reduceMotion) target.scrollIntoView({ block: "start" });
  }

  function normalizeLegacyContent() {
    const main = document.querySelector("#main");
    if (!main) return;
    main.querySelectorAll(".breadcrumbs").forEach(node => node.classList.add("doc-breadcrumbs"));
    const article = main.querySelector("article") || main;
    const h1 = article.querySelector("h1");
    if (h1 && !article.querySelector(".page-kicker")) {
      const kicker = document.createElement("p");
      kicker.className = "page-kicker";
      kicker.textContent = pageGroups.find(group => group.pages.some(([id]) => id === pageId))?.label || "Documentation";
      h1.before(kicker);
    }
    article.querySelectorAll(":scope > section").forEach(section => section.classList.add("doc-section"));
  }

  async function init() {
    await renderApiReference();
    normalizeLegacyContent();
    buildShell();
    addCopyButtons();
    buildToc();
    if (window.location.hash) window.setTimeout(revealHashTarget, 0);
  }

  init();
})();
