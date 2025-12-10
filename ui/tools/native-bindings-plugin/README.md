PoC TypeScript Language Service Plugin

Build:

  npm install
  npm run build

Install locally into your UI project (from repo root):

  cd ui
  npm install ../ui/tools/native-bindings-plugin

Then add to `ui/tsconfig.json`:

```json
{
  "compilerOptions": {
    "plugins": [
      { "name": "app-native-bindings-plugin", "indexPath": "src/generated/native-bindings.json" }
    ]
  }
}
```
