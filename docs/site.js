// Mend website: the repair animation, the tool gallery, a screenshot viewer,
// scroll reveals, and download links that point at the newest release's files
// for the visitor's system. Animation uses Motion (vendor/motion.js).
(function () {
  "use strict";

  var M = window.Motion;
  var root = document.documentElement;
  var motionOk = !!M && !window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  if (!motionOk) root.classList.remove("motion-ok");

  var REPO = "LukeHarrison1990/mend";
  var TEAL = "#40d6c4", NOISE = "#4a515e", FAULT = "#f2a65a";
  var EASE_OUT = [0.22, 1, 0.36, 1];

  // name, screenshot, description, categories, new?
  var TOOLS = [
    ["Voice", "voice.png", "One-step speech polish with presets for 28 microphones.", "speech mix", true],
    ["Spectral De-noise", "denoise.png", "Removes background noise: Voice mode for speech, Any sound for everything else.", "noise speech"],
    ["De-hum", "dehum.png", "Removes 50/60 Hz mains hum and its harmonics, and tracks drift.", "noise"],
    ["De-click", "declick.png", "Removes clicks and pops while leaving real transients alone.", "damage"],
    ["De-crackle", "decrackle.png", "For dense, tiny clicks: vinyl crackle and mouth clicks.", "damage speech"],
    ["De-clip", "declip.png", "Rebuilds the flattened peaks of clipped audio.", "damage"],
    ["De-plosive", "deplosive.png", "Tames p and b pops without touching the voice.", "speech"],
    ["De-ess", "deess.png", "Split-band sibilance reduction with look-ahead.", "speech"],
    ["Breath Control", "breathcontrol.png", "Finds breaths between phrases and turns them down.", "speech"],
    ["De-wind", "dewind.png", "Pulls down low rumble from wind and handling.", "noise"],
    ["De-reverb", "dereverb.png", "Reduces room reverb by predicting and removing its tail.", "noise"],
    ["Centre Extract", "centreextract.png", "Keeps or removes what both stereo channels share.", "mix"],
    ["EQ", "eq.png", "Six bands plus high- and low-pass filters.", "mix"],
    ["Leveler", "leveler.png", "Rides dialogue gain smoothly towards a loudness target.", "mix speech"]
  ];

  // ---- menu (small screens) ----
  var menuBtn = document.getElementById("menu-btn");
  var nav = document.getElementById("site-nav");
  function setMenu(open) {
    nav.classList.toggle("open", open);
    menuBtn.setAttribute("aria-expanded", open ? "true" : "false");
    menuBtn.textContent = open ? "Close" : "Menu";
  }
  menuBtn.addEventListener("click", function () { setMenu(!nav.classList.contains("open")); });
  nav.addEventListener("click", function (e) { if (e.target.closest("a")) setMenu(false); });
  document.addEventListener("keydown", function (e) {
    if (e.key === "Escape" && nav.classList.contains("open")) { setMenu(false); menuBtn.focus(); }
  });

  // ---- tool gallery ----
  var grid = document.getElementById("tool-grid");
  var cards = [];
  TOOLS.forEach(function (t) {
    var b = document.createElement("button");
    b.className = "tool";
    b.type = "button";
    b.dataset.cats = t[3];
    b.innerHTML =
      '<div class="thumb"><img loading="lazy" width="780" height="440" alt=""></div>' +
      '<div class="body"><h3></h3><p></p></div>';
    var img = b.querySelector("img");
    img.src = t[1];
    img.alt = "The " + t[0] + " plugin";
    var h = b.querySelector("h3");
    h.textContent = t[0];
    if (t[4]) {
      var n = document.createElement("span");
      n.className = "new";
      n.textContent = "NEW";
      h.appendChild(n);
    }
    b.querySelector("p").textContent = t[2];
    b.addEventListener("click", function () { openShot(img, t[0] + ": " + t[2]); });
    grid.appendChild(b);
    cards.push(b);
  });

  function enterCards(list, gap, dist) {
    if (!motionOk || !list.length) return;
    M.animate(list, { opacity: [0, 1], y: [dist, 0] },
      { duration: 0.45, ease: EASE_OUT, delay: M.stagger(gap) });
  }
  if (motionOk) {
    M.inView(grid, function () { enterCards(cards, 0.04, 16); }, { amount: 0.1 });
  }

  // ---- tool filters ----
  var filters = document.getElementById("filters");
  var filterStatus = document.getElementById("filter-status");
  filters.addEventListener("click", function (e) {
    var btn = e.target.closest("button");
    if (!btn || btn.getAttribute("aria-pressed") === "true") return;
    filters.querySelectorAll("button").forEach(function (x) {
      x.setAttribute("aria-pressed", x === btn ? "true" : "false");
    });
    var f = btn.dataset.filter;
    var shown = [];
    cards.forEach(function (c) {
      var on = f === "all" || c.dataset.cats.split(" ").indexOf(f) !== -1;
      c.hidden = !on;
      if (on) shown.push(c);
    });
    filterStatus.textContent = "Showing " + shown.length + " tools" +
      (f === "all" ? "" : " for " + btn.textContent.toLowerCase());
    enterCards(shown, 0.03, 10);
    if (!motionOk) shown.forEach(function (c) { c.style.opacity = ""; });
  });

  // ---- screenshot viewer ----
  var dlg = document.getElementById("lightbox");
  var dFig = dlg.querySelector("figure");
  var dImg = document.getElementById("lightbox-img");
  var dCap = document.getElementById("lightbox-cap");
  var closing = false;

  function openShot(src, caption) {
    var nw = src.naturalWidth || +src.getAttribute("width");
    var nh = src.naturalHeight || +src.getAttribute("height");
    var maxW = Math.min(1152, window.innerWidth - 48);
    var maxH = window.innerHeight * 0.8;
    var k = Math.min(maxW / nw, maxH / nh, 1);
    dImg.style.width = Math.round(nw * k) + "px";
    dImg.style.height = Math.round(nh * k) + "px";
    dImg.src = src.currentSrc || src.src;
    dImg.alt = caption;
    dCap.textContent = caption;
    if (motionOk) M.animate(dFig, { opacity: 1, scale: 1 }, { duration: 0 });
    dlg.showModal();
    if (!motionOk) return;

    // grow the picture out of the one that was clicked
    var from = src.getBoundingClientRect();
    var to = dImg.getBoundingClientRect();
    if (!to.width || !from.width) return;
    var s = from.width / to.width;
    var dx = from.left + from.width / 2 - (to.left + to.width / 2);
    var dy = from.top + from.height / 2 - (to.top + to.height / 2);
    M.animate(dImg, { x: [dx, 0], y: [dy, 0], scale: [s, 1] },
      { type: M.spring, bounce: 0.12, visualDuration: 0.38 });
    M.animate(dCap, { opacity: [0, 1] }, { duration: 0.2, delay: 0.18 });
  }

  function closeShot() {
    if (!dlg.open || closing) return;
    if (!motionOk) { dlg.close(); return; }
    closing = true;
    M.animate(dFig, { opacity: 0, scale: 0.97 }, { duration: 0.15, ease: "easeIn" }).finished
      .then(function () { dlg.close(); closing = false; });
  }

  dlg.addEventListener("click", closeShot);
  dlg.addEventListener("cancel", function (e) { e.preventDefault(); closeShot(); });
  document.querySelectorAll(".hero-shot img, .shot img").forEach(function (img) {
    img.addEventListener("click", function () { openShot(img, img.alt); });
  });

  // ---- repair animation: a damaged waveform mended as a scan line passes ----
  var barsEl = document.getElementById("bars");
  var scan = document.getElementById("scan");
  var stateEl = document.getElementById("repair-state");
  var replay = document.getElementById("repair-replay");
  var bars = [], clean = [], broken = [], fault = [];
  var running = [], runId = 0, repairStarted = false, repairDone = false;

  function rng(seed) {
    return function () {
      seed = (seed * 1664525 + 1013904223) >>> 0;
      return seed / 4294967296;
    };
  }

  function buildBars() {
    var n = Math.max(40, Math.min(120, Math.round(barsEl.clientWidth / 8)));
    if (n === bars.length) return false;
    var r = rng(7);
    barsEl.textContent = "";
    bars = []; clean = []; broken = []; fault = [];
    for (var i = 0; i < n; i++) {
      var x = i / (n - 1);
      var syllable = Math.abs(Math.sin(x * Math.PI * 9 + Math.sin(x * 5) * 1.2));
      var phrase = 0.55 + 0.45 * Math.sin(x * Math.PI * 2.2 + 0.6);
      var c = Math.min(0.9, 0.08 + 0.78 * Math.pow(syllable, 1.6) * phrase + r() * 0.06);
      var b = Math.max(c, 0.24 + r() * 0.16);
      var bad = false;
      if (c > 0.58 && ((x > 0.18 && x < 0.36) || (x > 0.5 && x < 0.64))) { b = 1; bad = true; }
      clean.push(c); broken.push(b); fault.push(bad);
    }
    [0.09, 0.43, 0.71, 0.9].forEach(function (f) {
      var k = Math.round(f * (n - 1));
      broken[k] = 0.96 + r() * 0.04;
      fault[k] = true;
    });
    for (var j = 0; j < n; j++) {
      var bar = document.createElement("i");
      barsEl.appendChild(bar);
      bars.push(bar);
    }
    return true;
  }

  function stopRepair() {
    runId++;
    running.forEach(function (a) { a.stop(); });
    running = [];
  }

  function paint(after) {
    for (var i = 0; i < bars.length; i++) {
      bars[i].style.transform = "scaleY(" + (after ? clean[i] : broken[i]) + ")";
      bars[i].style.backgroundColor = after ? TEAL : (fault[i] ? FAULT : NOISE);
    }
    scan.style.opacity = "0";
    stateEl.dataset.state = after ? "after" : "before";
    stateEl.textContent = after ? "After: mended" : "Before: hiss, clicks, clipping";
  }

  function runRepair() {
    stopRepair();
    paint(false);
    replay.hidden = true;
    repairStarted = true;
    repairDone = false;
    var id = runId;
    var D = 2.6, lead = 0.35;
    var wave = barsEl.parentNode;
    var w = wave.clientWidth;
    running.push(M.animate(scan, { x: [0, w - 2], opacity: [0, 1, 1, 0] },
      { duration: D, delay: lead, ease: "linear", opacity: { duration: D, delay: lead, times: [0, 0.04, 0.94, 1] } }));
    var base = barsEl.offsetLeft;
    for (var i = 0; i < bars.length; i++) {
      var bar = bars[i];
      var at = lead + D * (base + bar.offsetLeft + bar.offsetWidth / 2) / w;
      running.push(M.animate(bar,
        { scaleY: [broken[i], clean[i]], backgroundColor: [fault[i] ? FAULT : NOISE, TEAL] },
        {
          scaleY: { type: M.spring, bounce: 0.35, visualDuration: 0.32, delay: at },
          backgroundColor: { duration: 0.25, delay: at }
        }));
    }
    setTimeout(function () {
      if (id !== runId) return;
      repairDone = true;
      stateEl.dataset.state = "after";
      stateEl.textContent = "After: mended";
      replay.hidden = false;
    }, (lead + D + 0.2) * 1000);
  }

  buildBars();
  if (motionOk) {
    paint(false);
    M.inView(document.getElementById("repair"), function () { runRepair(); }, { amount: 0.6 });
    replay.addEventListener("click", runRepair);
  } else {
    paint(true);
  }
  var resizeTimer;
  window.addEventListener("resize", function () {
    clearTimeout(resizeTimer);
    resizeTimer = setTimeout(function () {
      if (!buildBars()) return;
      if (motionOk && !repairDone) {
        paint(false);
        if (repairStarted) runRepair();
      } else {
        stopRepair();
        paint(true);
      }
    }, 200);
  });

  // ---- scroll reveals, counters, current section ----
  if (motionOk) {
    M.inView(".reveal", function (el) {
      M.animate(el, { opacity: [0, 1], y: [24, 0] }, { duration: 0.6, ease: EASE_OUT });
    }, { margin: "0px 0px -8% 0px" });

    M.inView(".strip", function (strip) {
      strip.querySelectorAll("[data-count]").forEach(function (el) {
        var to = +el.dataset.count;
        M.animate(0, to, {
          duration: 0.9, ease: "easeOut", delay: 0.15,
          onUpdate: function (v) { el.textContent = Math.round(v); }
        });
      });
    }, { amount: 0.6 });
  }

  if (M) {
    var links = {};
    nav.querySelectorAll('a[href^="#"]').forEach(function (a) { links[a.getAttribute("href").slice(1)] = a; });
    M.inView(Object.keys(links).map(function (id) { return document.getElementById(id); }), function (sec) {
      Object.keys(links).forEach(function (id) {
        if (id === sec.id) links[id].setAttribute("aria-current", "true");
        else links[id].removeAttribute("aria-current");
      });
      return function () {
        if (links[sec.id].getAttribute("aria-current")) links[sec.id].removeAttribute("aria-current");
      };
    }, { margin: "-45% 0px -50% 0px" });
  }

  // ---- downloads ----
  function detectOs() {
    var p = ((navigator.userAgentData && navigator.userAgentData.platform) || navigator.platform || "") +
      " " + navigator.userAgent;
    if (/iPhone|iPad|Android/i.test(p)) return null; // nothing to install there
    if (/Win/i.test(p)) return "windows";
    if (/Mac/i.test(p)) return "macos";
    if (/Linux|X11/i.test(p)) return "linux";
    return null;
  }
  var os = detectOs();
  var names = { windows: "Windows", macos: "macOS", linux: "Linux" };
  var mine = os && document.querySelector('.dl[data-os="' + os + '"]');
  if (mine) mine.classList.add("mine");

  var primary = document.getElementById("primary-download");
  var finalDl = document.getElementById("final-download");
  if (os) {
    primary.textContent = "Download for " + names[os];
    finalDl.textContent = "Get Mend for " + names[os];
  }

  // Point each button at the matching file of the newest release. Without
  // the API (offline, rate-limited) they stay on the releases page.
  fetch("https://api.github.com/repos/" + REPO + "/releases/latest")
    .then(function (r) { return r.ok ? r.json() : Promise.reject(r.status); })
    .then(function (rel) {
      var assets = rel.assets || [];
      function find(suffix) {
        for (var i = 0; i < assets.length; i++)
          if (assets[i].name.slice(-suffix.length) === suffix) return assets[i];
        return null;
      }
      document.querySelectorAll("[data-asset]").forEach(function (a) {
        var f = find(a.getAttribute("data-asset"));
        if (f) {
          a.href = f.browser_download_url;
        } else if (a.classList.contains("button")) {
          a.textContent += " (coming soon)";
        }
      });
      if (os && mine) {
        var main = mine.querySelector("[data-asset]");
        if (main && find(main.getAttribute("data-asset"))) {
          primary.href = main.href;
          finalDl.href = main.href;
        }
      }
      var version = (rel.tag_name || "").replace(/^v/, "");
      var date = rel.published_at ? new Date(rel.published_at) : null;
      document.getElementById("release-line").textContent =
        "Version " + version + (date ? ", released " + date.toLocaleDateString(undefined,
          { day: "numeric", month: "long", year: "numeric" }) : "") +
        " · MIT licence · no account, no subscription, no telemetry";
      document.getElementById("download-lede").textContent =
        "Mend " + version + ". Every release is built from the public source by GitHub.";
    })
    .catch(function () { /* keep the releases-page links */ });
})();
