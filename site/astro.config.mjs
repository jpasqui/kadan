import starlight from '@astrojs/starlight';
import { defineConfig } from 'astro/config';
import starlightLinksValidator from 'starlight-links-validator';
import starlightSidebarTopics from 'starlight-sidebar-topics';
import { editInPlace } from './scripts/edit-in-place.mjs';
import { sections } from './src/data/sections.mjs';

// The address the site is served from. Set SITE_URL when building for a host, so that the
// sitemap and the links other sites show point at the right place.
const site = process.env.SITE_URL;

// Each section's sidebar. The sections themselves are in src/data/sections.mjs. A group's name
// is one word.
const sidebars = {
  Guides: [
    { label: 'Guides', slug: 'guides' },
    {
      label: 'Start',
      items: [
        { label: 'Install Kadan', slug: 'guides/install' },
        { label: 'Import from the game', slug: 'guides/import' },
      ],
    },
    {
      label: 'Create',
      items: [
        { label: 'Create a map', slug: 'guides/create-a-map', badge: { text: 'WIP', variant: 'caution' } },
        { label: 'Edit a map', slug: 'guides/edit-a-map', badge: { text: 'WIP', variant: 'caution' } },
        { label: 'Create an enemy', slug: 'guides/create-an-enemy', badge: { text: 'WIP', variant: 'caution' } },
        { label: 'Export a mod', slug: 'guides/export' },
        { label: 'Work on several mods', slug: 'guides/several-mods' },
      ],
    },
    {
      label: 'Reference',
      items: [{ label: 'The Kadan panel', slug: 'guides/kadan-panel' }],
    },
    {
      label: 'Help',
      items: [
        { label: 'Troubleshooting', slug: 'guides/troubleshooting' },
        { label: 'Report a problem', slug: 'guides/get-help' },
      ],
    },
  ],
};

export default defineConfig({
  ...(site ? { site } : {}),
  integrations: [
    // Editing a page's text in the browser, while `npm run dev` runs. It does nothing in a build.
    editInPlace(),
    starlight({
      title: 'Kadan',
      logo: { src: './src/assets/kadan-logo.svg', alt: '' },
      favicon: '/favicon.svg',
      social: [{ icon: 'discord', label: 'Pikmin 4 Modding Discord', href: 'https://discord.gg/Rg5BnvuwmQ' }],
      description: 'The Pikmin 4 modding plugin for Unreal Engine.',
      plugins: [
        // A broken link or anchor fails the build.
        starlightLinksValidator(),
        // The links at the top of every page are also the list at the top of the menu on a
        // narrow screen. A section has a sidebar, and a link to another site has none.
        starlightSidebarTopics(
          sections.map((section) => ({ label: section.label, link: section.link, items: sidebars[section.label] })),
          // The first page has no sidebar and belongs to no section.
          { exclude: ['/'] },
        ),
      ],
      components: {
        // The site's name, followed by the links to its sections.
        SiteTitle: './src/components/SiteTitle.astro',
        // The menu on a narrow screen, on the pages that have no sidebar too.
        PageFrame: './src/components/PageFrame.astro',
        // The site builder's footer, and a line of the site's own under it.
        Footer: './src/components/Footer.astro',
      },
      customCss: [
        '@fontsource-variable/nunito',
        '@fontsource-variable/inter',
        '@fontsource-variable/jetbrains-mono',
        './src/styles/site.css',
      ],
      lastUpdated: false,
      pagination: true,
    }),
  ],
});
