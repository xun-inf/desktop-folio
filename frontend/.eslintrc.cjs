const { createRequire } = require('node:module');
const lintRequire = createRequire(
  require('node:path').join(__dirname, 'tools/eslint/package.json'),
);

module.exports = {
  root: true,
  env: {
    es2022: true,
  },
  extends: ['eslint:recommended'],
  parserOptions: {
    ecmaVersion: 'latest',
    sourceType: 'module',
  },
  ignorePatterns: ['node_modules/', 'dist/', '.artifacts/'],
  overrides: [
    {
      files: ['**/*.{ts,tsx}'],
      parser: lintRequire.resolve('@typescript-eslint/parser'),
      extends: ['plugin:@typescript-eslint/recommended'],
    },
    {
      files: ['src/app/**/*.{js,jsx,ts,tsx}', 'scripts/sticker-preview-ui-fixture.jsx'],
      env: {
        browser: true,
      },
      extends: [
        'plugin:react/recommended',
        'plugin:react/jsx-runtime',
        'plugin:react-hooks/recommended',
      ],
      parserOptions: {
        ecmaFeatures: {
          jsx: true,
        },
      },
      settings: {
        react: {
          version: 'detect',
        },
      },
    },
    {
      files: ['src/app/**/*.{ts,tsx}'],
      rules: {
        'react/prop-types': 'off',
      },
    },
    {
      files: [
        'src/{main,preload,ipc,build}/**/*.{js,ts}',
        'scripts/**/*.{js,mjs,cjs}',
        '*.{js,mjs,cjs}',
      ],
      env: {
        node: true,
      },
    },
  ],
};
