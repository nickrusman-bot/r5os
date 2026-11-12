// SPDX-FileCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The R5Book frame: the top bar, the contents on the left and the footer,
// the same on every page (as site-nav.js does for the website). It also
// defines the line glyphs with the same geometry as distro/icons/R5Icons.cpp.
(function () {
	const page = document.body.dataset.page || "index";

	const contents = [
		{ title: "The R5Book", items: [
			["index.html", "Introduction"],
			["index.html#linking", "Linking a program"],
			["index.html#rules", "Rules for LibR5"],
		]},
		{ title: "Icons Kit", file: "icons-kit.html", items: [
			["icons-kit.html", "Overview"],
			["icons-kit.html#r5_icon", "r5_icon", true],
			["icons-kit.html#icon-bitmaps", "Icon bitmaps", true],
			["icons-kit.html#icon-buttons", "Icon buttons", true],
			["icons-kit.html#r5_glyph", "r5_glyph", true],
		]},
		{ title: "Appearance Kit", file: "appearance-kit.html", items: [
			["appearance-kit.html", "Overview"],
			["appearance-kit.html#BCard", "BCard", true],
			["appearance-kit.html#BPageHeader", "BPageHeader", true],
			["appearance-kit.html#BActionCard", "BActionCard", true],
			["appearance-kit.html#BSidebar", "BSidebar", true],
			["appearance-kit.html#BSidebarItem", "BSidebarItem", true],
			["appearance-kit.html#BScreenArrangement", "BScreenArrangement", true],
			["appearance-kit.html#BUserCard", "BUserCard", true],
			["appearance-kit.html#BUserListItem", "BUserListItem", true],
			["appearance-kit.html#BPasswordControl", "BPasswordControl", true],
			["appearance-kit.html#BAccentButton", "BAccentButton", true],
			["appearance-kit.html#BIconView", "BIconView", true],
			["appearance-kit.html#functions", "Functions", true],
		]},
		{ title: "MIDI 2 Kit", file: "midi2-kit.html", items: [
			["midi2-kit.html", "Overview"],
			["midi2-kit.html#sending", "Sending", true],
			["midi2-kit.html#ump", "The packet", true],
			["midi2-kit.html#receiving", "Receiving", true],
			["midi2-kit.html#per-note", "Per-note controllers", true],
			["midi2-kit.html#property-exchange", "Property Exchange", true],
			["midi2-kit.html#profiles", "Profiles", true],
			["midi2-kit.html#discovery", "Discovery", true],
			["midi2-kit.html#usb", "USB descriptors", true],
			["midi2-kit.html#drivers", "Drivers", true],
		]},
		{ title: "Vulkan", file: "vulkan.html", items: [
			["vulkan.html", "Vulkan in a window"],
			["vulkan.html#surface", "vkCreateViewSurfaceRFIVE", true],
			["vulkan.html#present", "Present results", true],
			["vulkan.html#fallback", "GPU or software", true],
			["vulkan.html#porting", "Porting a program", true],
		]},
		{ title: "Haiku kits", file: "haiku-additions.html", items: [
			["haiku-additions.html", "Additions to Haiku kits"],
			["haiku-additions.html#BLocaleRoster", "BLocaleRoster", true],
			["haiku-additions.html#BFormattingConventions", "BFormattingConventions", true],
		]},
		{ title: "Guidelines", file: "guidelines.html", items: [
			["guidelines.html", "Making it look like R5"],
			["guidelines.html#forms", "Forms", true],
			["guidelines.html#cards", "Cards", true],
			["guidelines.html#actions", "Buttons", true],
			["guidelines.html#icons", "Icons", true],
			["guidelines.html#settings", "Settings pages", true],
			["guidelines.html#people", "People and accounts", true],
			["guidelines.html#words", "Words", true],
		]},
	];

	const pebble = `
		<svg viewBox="0 0 100 100" aria-hidden="true">
			<defs>
				<linearGradient id="r5book-grad" x1="12%" y1="12%" x2="88%" y2="88%">
					<stop offset="0%" stop-color="#0084ff"/>
					<stop offset="45%" stop-color="#3898ff"/>
					<stop offset="72%" stop-color="#65b891"/>
					<stop offset="100%" stop-color="#ffb800"/>
				</linearGradient>
			</defs>
			<path d="M50,8 C74,8 92,25 92,49 C92,74 73,92 49,92 C25,92 8,75 8,50 C8,24 27,8 50,8 Z
				M54,24 C42,24 37,32 43,42 C49,51 46,62 35,66 C26,69 27,77 41,77 C64,77 77,64 77,48 C77,33 66,24 54,24 Z"
				fill="url(#r5book-grad)" fill-rule="evenodd"/>
			<path d="M22,29 C30,17 44,12 58,14" fill="none" stroke="#ffffff"
				stroke-width="2.5" stroke-linecap="round" opacity="0.38"/>
		</svg>`;

	const header = document.createElement("header");
	header.className = "site-nav";
	header.innerHTML = `
		<a href="index.html" class="brand-lockup">
			${pebble}
			<div class="brand-lockup-text"><strong>R5Book</strong><span>for developers</span></div>
		</a>
		<nav class="nav-links">
			<a href="index.html" class="${page === "index" ? "active" : ""}">Introduction</a>
			<a href="icons-kit.html" class="${page === "icons" ? "active" : ""}">Icons Kit</a>
			<a href="appearance-kit.html" class="${page === "appearance" ? "active" : ""}">Appearance Kit</a>
			<a href="midi2-kit.html" class="${page === "midi2" ? "active" : ""}">MIDI 2 Kit</a>
			<a href="vulkan.html" class="${page === "vulkan" ? "active" : ""}">Vulkan</a>
			<a href="guidelines.html" class="${page === "guidelines" ? "active" : ""}">Guidelines</a>
			<a href="https://github.com/r5-operating-system/r5os" class="nav-pill">GitHub</a>
		</nav>`;
	document.body.prepend(header);

	const toc = document.querySelector("aside.toc");
	if (toc) {
		const here = location.pathname.split("/").pop() || "index.html";
		toc.innerHTML = contents.map((group) => `
			<h4>${group.title}</h4>
			${group.items.map(([href, label, sub]) => {
				const current = href === here ? " current" : "";
				return `<a href="${href}" class="${sub ? "sub" : ""}${current}">${label}</a>`;
			}).join("")}`).join("");
	}

	const footer = document.createElement("footer");
	footer.className = "site-footer";
	footer.innerHTML = `
		<div><strong>R5 Operating System</strong> &nbsp;&bull;&nbsp; R5Book, LibR5 developer guide</div>
		<div>
			<a href="index.html">R5Book</a>
			<a href="../LIBR5.md">LIBR5.md</a>
			<a href="../ICONS.md">ICONS.md</a>
			<a href="https://github.com/r5-operating-system/r5os">GitHub</a>
		</div>`;
	document.body.append(footer);
})();


// The line glyphs of libr5icons.so, on the same 24 x 24 grid with a 2.2
// unit pen as glyph_paint() in R5Icons.cpp. Use <svg data-glyph="power">.
const R5_GLYPHS = {
	power: '<path d="M5.64 6.64 A9 9 0 1 0 18.36 6.64"/><path d="M12 2 V12"/>',
	restart: '<path d="M20.23 7.25 A9.5 9.5 0 1 0 20.61 16.01"/><path d="M20.5 3 V8.5 H15"/>',
	clock: '<circle cx="12" cy="12" r="10"/><path d="M12 6 V12 L16 14"/>',
	eye: '<path d="M1 12 A11.82 11.82 0 0 1 23 12"/><path d="M1 12 A11.82 11.82 0 0 0 23 12"/>'
		+ '<circle cx="12" cy="12" r="3"/>',
	"eye-off": '<g mask="url(#r5book-slash)"><path d="M1 12 A11.82 11.82 0 0 1 23 12"/>'
		+ '<path d="M1 12 A11.82 11.82 0 0 0 23 12"/><circle cx="12" cy="12" r="3"/></g>'
		+ '<path d="M2 2 L22 22"/>',
	"log-out": '<path d="M9 3 H4 V21 H9"/><path d="M9 12 H21"/><path d="M16 7 L21 12 L16 17"/>',
};

(function () {
	const defs = document.createElementNS("http://www.w3.org/2000/svg", "svg");
	defs.setAttribute("width", "0");
	defs.setAttribute("height", "0");
	defs.style.position = "absolute";
	defs.innerHTML = '<mask id="r5book-slash" maskUnits="userSpaceOnUse" x="0" y="0" width="24" height="24">'
		+ '<rect width="24" height="24" fill="white"/>'
		+ '<path d="M1 1 L23 23" stroke="black" stroke-width="4.8"/></mask>';
	document.body.append(defs);

	document.querySelectorAll("svg[data-glyph]").forEach((svg) => {
		svg.setAttribute("viewBox", "0 0 24 24");
		svg.setAttribute("fill", "none");
		svg.setAttribute("stroke", svg.getAttribute("stroke") || "currentColor");
		svg.setAttribute("stroke-width", "2.2");
		svg.setAttribute("stroke-linecap", "round");
		svg.setAttribute("stroke-linejoin", "round");
		svg.innerHTML = R5_GLYPHS[svg.dataset.glyph] || "";
	});
})();
