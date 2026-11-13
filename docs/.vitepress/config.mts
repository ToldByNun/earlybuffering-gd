import { defineConfig } from 'vitepress'

// GitHub Pages project site: https://ToldByNun.github.io/earlybuffering-gd/
const repoBase = '/earlybuffering-gd/'

export default defineConfig({
  title: 'EarlyBuffer',
  description: 'Easy docs for EarlyBuffer — install, settings, features, how it works, and developer notes.',
  lang: 'en-US',
  base: repoBase,
  cleanUrls: true,
  lastUpdated: true,
  ignoreDeadLinks: true,
  head: [
    ['meta', { name: 'theme-color', content: '#1a1b26' }],
  ],
  themeConfig: {
    logo: undefined,
    siteTitle: 'EarlyBuffer',
    nav: [
      { text: 'Start', link: '/start' },
      { text: 'Install', link: '/install/' },
      { text: 'Features', link: '/features/' },
      { text: 'Configure', link: '/configure/' },
      { text: 'How it works', link: '/how-it-works/' },
      { text: 'Problems', link: '/troubleshoot/' },
      { text: 'Dev', link: '/dev/' },
      {
        text: 'v1.6.1',
        items: [
          { text: 'GitHub', link: 'https://github.com/ToldByNun/earlybuffering-gd' },
          { text: 'Releases', link: 'https://github.com/ToldByNun/earlybuffering-gd/releases' },
          { text: 'Discord', link: 'https://discord.gg/QJuTDkmPcf' },
        ],
      },
    ],
    sidebar: {
      '/dev/': [
        {
          text: 'Developer',
          items: [
            { text: 'Dev overview', link: '/dev/' },
            { text: 'Build & package', link: '/dev/build' },
            { text: 'Source layout', link: '/dev/architecture' },
            { text: 'Planner pipeline', link: '/dev/planner' },
            { text: 'Settings & mod.json', link: '/dev/settings' },
            { text: 'Dev traces & reports', link: '/dev/traces' },
            { text: 'Tests & style', link: '/dev/testing' },
            { text: 'Contributing', link: '/dev/contributing' },
          ],
        },
        {
          text: 'Deep reference files',
          collapsed: false,
          items: [
            { text: 'Assist reference', link: '/assist_reference' },
            { text: 'Cloud configs (tech)', link: '/cloud_configs' },
            { text: 'Source architecture', link: '/source_architecture' },
            { text: 'Assist contracts 1.4.0', link: '/assist_contracts_140' },
          ],
        },
        {
          text: 'Player docs',
          items: [
            { text: 'Home', link: '/' },
            { text: 'Install', link: '/install/' },
            { text: 'Configure', link: '/configure/' },
          ],
        },
      ],
      '/': [
        {
          text: 'Getting started',
          items: [
            { text: 'What is EarlyBuffer?', link: '/' },
            { text: 'Quick start', link: '/start' },
          ],
        },
        {
          text: 'Install',
          items: [
            { text: 'Install overview', link: '/install/' },
            { text: 'GitHub Releases', link: '/install/github-releases' },
            { text: 'Phone (Android / iOS)', link: '/install/mobile' },
            { text: 'Build from source', link: '/install/from-source' },
            { text: 'Geode Index (banned)', link: '/install/geode-index' },
          ],
        },
        {
          text: 'Features',
          items: [
            { text: 'Feature list', link: '/features/' },
            { text: 'Assist modes', link: '/features/modes' },
            { text: 'Adaptive Plan', link: '/features/adaptive' },
            { text: 'Cloud Configs', link: '/features/cloud-configs' },
            { text: 'Safe Mode', link: '/features/safe-mode' },
            { text: 'Bake & Trigger World', link: '/features/bake' },
          ],
        },
        {
          text: 'Configure',
          items: [
            { text: 'Settings map', link: '/configure/' },
            { text: 'Main & Window', link: '/configure/main' },
            { text: 'Assist', link: '/configure/assist' },
            { text: 'Where it runs (Assist)', link: '/configure/scope' },
            { text: 'Per game-mode', link: '/configure/modes' },
            { text: 'Keys & extras', link: '/configure/keys' },
          ],
        },
        {
          text: 'How it works',
          items: [
            { text: 'Simple overview', link: '/how-it-works/' },
            { text: 'Ticks vs frames', link: '/how-it-works/ticks' },
            { text: 'Shadow sim', link: '/how-it-works/shadow-sim' },
            { text: 'Pass vs Delay', link: '/how-it-works/pass-vs-delay' },
          ],
        },
        {
          text: 'Problems',
          items: [
            { text: 'Common fixes', link: '/troubleshoot/' },
            { text: 'Feels wrong / too strong', link: '/troubleshoot/feels-wrong' },
            { text: 'Mobile package errors', link: '/troubleshoot/mobile' },
            { text: 'Lag & crashes', link: '/troubleshoot/lag' },
          ],
        },
        {
          text: 'Developer',
          collapsed: false,
          items: [
            { text: 'Dev section', link: '/dev/' },
            { text: 'Planner pipeline', link: '/dev/planner' },
            { text: 'Traces & reports', link: '/dev/traces' },
          ],
        },
        {
          text: 'Deep reference',
          collapsed: true,
          items: [
            { text: 'Assist reference', link: '/assist_reference' },
            { text: 'Cloud configs (tech)', link: '/cloud_configs' },
            { text: 'Source architecture', link: '/source_architecture' },
          ],
        },
      ],
    },
    socialLinks: [
      { icon: 'github', link: 'https://github.com/ToldByNun/earlybuffering-gd' },
      { icon: 'discord', link: 'https://discord.gg/QJuTDkmPcf' },
    ],
    search: {
      provider: 'local',
    },
    editLink: {
      pattern: 'https://github.com/ToldByNun/earlybuffering-gd/edit/earlybuffer-1.6.1-dev/docs/:path',
      text: 'Edit this page on GitHub',
    },
    footer: {
      message: 'EarlyBuffer docs · players + developers',
      copyright: 'MIT · Audesign',
    },
  },
})
