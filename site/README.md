# Kadan docs site

The website for Kadan, built with Astro and Starlight. It uses the same design as the Tsuchi
docs site, with its own logo and a green accent.

    npm install
    npm run dev      # the site with live reload, and the Edit text button for changing text in the browser
    npm run build    # the finished site in dist/

Set `SITE_URL` when building for the host: `SITE_URL=https://kadan.jpasqui.dev npm run build`.

- Guides are Markdown in `src/content/docs/guides`. The sidebar is in `astro.config.mjs`.
- The links at the top of every page are in `src/data/sections.mjs`.
- The logo is `src/assets/kadan-logo.svg`, a copy of `docs/branding/kadan-flowerbed.svg`. The
  tab icon is the same file in `public/favicon.svg`.
- The colors are in `src/styles/site.css`.
- The text follows the Tsuchi repository's `STYLE.md`, sections Voice and Natural wording.

A broken link fails the build.
