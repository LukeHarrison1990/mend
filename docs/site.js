// Mend website: the tool gallery, a screenshot viewer, and download links
// that point at the newest release's files for the visitor's system.
(function () {
  "use strict";

  var REPO = "LukeHarrison1990/mend";

  var TOOLS = [
    ["Voice", "voice.png", "One-step speech polish with presets for 28 microphones.", true],
    ["Spectral De-noise", "denoise.png", "Removes background noise: Voice mode for speech, Any sound for everything else."],
    ["De-hum", "dehum.png", "Removes 50/60 Hz mains hum and its harmonics, and tracks drift."],
    ["De-click", "declick.png", "Removes clicks and pops while leaving real transients alone."],
    ["De-crackle", "decrackle.png", "For dense, tiny clicks: vinyl crackle and mouth clicks."],
    ["De-clip", "declip.png", "Rebuilds the flattened peaks of clipped audio."],
    ["De-plosive", "deplosive.png", "Tames p and b pops without touching the voice."],
    ["De-ess", "deess.png", "Split-band sibilance reduction with look-ahead."],
    ["Breath Control", "breathcontrol.png", "Finds breaths between phrases and turns them down."],
    ["De-wind", "dewind.png", "Pulls down low rumble from wind and handling."],
    ["De-reverb", "dereverb.png", "Reduces room reverb by predicting and removing its tail."],
    ["Centre Extract", "centreextract.png", "Keeps or removes what both stereo channels share."],
    ["EQ", "eq.png", "Six bands plus high- and low-pass filters."],
    ["Leveler", "leveler.png", "Rides dialogue gain smoothly towards a loudness target."]
  ];

  // ---- tool gallery ----
  var grid = document.getElementById("tool-grid");
  TOOLS.forEach(function (t) {
    var b = document.createElement("button");
    b.className = "tool";
    b.type = "button";
    b.innerHTML =
      '<img loading="lazy" width="780" height="440" alt="">' +
      "<div><h3></h3><p></p></div>";
    b.querySelector("img").src = t[1];
    b.querySelector("img").alt = "The " + t[0] + " plugin";
    b.querySelector("h3").textContent = t[0];
    if (t[3]) {
      var n = document.createElement("span");
      n.className = "new";
      n.textContent = "NEW";
      b.querySelector("h3").appendChild(n);
    }
    b.querySelector("p").textContent = t[2];
    b.addEventListener("click", function () { show(t[1], t[0] + ": " + t[2]); });
    grid.appendChild(b);
  });

  // ---- screenshot viewer ----
  var box = document.getElementById("lightbox");
  var boxImg = document.getElementById("lightbox-img");
  var boxCap = document.getElementById("lightbox-cap");
  function show(src, caption) {
    boxImg.src = src;
    boxImg.alt = caption;
    boxCap.textContent = caption;
    box.hidden = false;
  }
  box.addEventListener("click", function () { box.hidden = true; });
  document.addEventListener("keydown", function (e) { if (e.key === "Escape") box.hidden = true; });
  document.querySelectorAll(".hero-shot img, .shot img").forEach(function (img) {
    img.style.cursor = "zoom-in";
    img.addEventListener("click", function () { show(img.getAttribute("src"), img.alt); });
  });

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
  if (os) primary.textContent = "Download for " + names[os];

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
        if (main && find(main.getAttribute("data-asset"))) primary.href = main.href;
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
