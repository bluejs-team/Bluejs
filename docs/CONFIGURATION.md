# Configuration

Project builds (`blue -build <dir>`) require `blue.config.json` at the project root.

## Minimal example

```json
{
  "entry": "src/main.js"
}
```

## Hybrid example

```json
{
  "entry": "src/main.js",
  "quickjsIsland": "src/island.js",
  "hybrid": true,
  "bundlePlatform": "node",
  "publicDir": "public",
  "bundleDependencies": false
}
```

## Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `entry` | string | `src/main.js` | AOT JavaScript entry file (compiled to C++) |
| `quickjsIsland` | string | - | QuickJS island entry file (bundled with esbuild). Required when `hybrid: true` |
| `hybrid` | boolean | `false` | Enable dual-entry mode (AOT + Island). Mutually exclusive with `controlPlane` |
| `controlPlane` | boolean | `false` | Run entire entry in QuickJS only. Mutually exclusive with `hybrid` |
| `bundlePlatform` | string | `neutral` | esbuild platform: `node`, `browser`, or `neutral` |
| `publicDir` | string | `public` | Directory for static assets to embed |
| `bundleDependencies` | boolean | `false` | Run esbuild on the AOT entry (usually `false` for hybrid) |

## HTML embedding

If `public/index.html` exists and your entry file contains the literal `__BLUE_BUNDLE_HTML__`, the build replaces it with the inlined HTML content. This is how UIs ship inside the binary.

## React auto-detection

If `package.json` lists `react` or `react-dom` and `src/frontend.{jsx,tsx,js,ts}` exists, the build automatically runs esbuild to produce `public/app.bundle.js`.

## Validation errors

| Condition | Result |
|-----------|--------|
| `hybrid` and `controlPlane` both true | Build fails |
| `hybrid` true but `quickjsIsland` missing | Build fails |
| Entry has `__BLUE_BUNDLE_HTML__` but no `public/index.html` | Build fails |
