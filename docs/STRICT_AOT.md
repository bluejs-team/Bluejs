# Strict AOT JavaScript Support

Blue's **Strict AOT** mode compiles JavaScript directly to C++. The compiler runs your code through Babel (preset-env, IE11 loose mode) before emitting, which handles most modern syntax automatically. The result is a much larger supported subset than older docs described.

If your code uses genuinely unsupported features, move that code to a [QuickJS Island](HYBRID.md) - it supports full ES2020+ and npm packages.

---

## What Babel lowers automatically

These features work in AOT because Babel rewrites them before the emitter sees them:

| Feature | Example |
|---------|---------|
| Arrow functions | `const f = x => x * 2` |
| Template literals | `` `Hello ${name}` `` |
| Destructuring (object + array) | `const { a, b } = obj`, `const [x, y] = arr` |
| Default parameters | `function f(x = 0) {}` |
| Rest parameters | `function f(...args) {}` |
| Spread in calls | `fn(...arr)` |
| `for...of` loops | `for (const x of arr) {}` |
| Classes and inheritance | `class Dog extends Animal {}` |
| Nullish coalescing | `x ?? "default"` |
| Optional chaining | `obj?.prop` |
| Shorthand methods / computed properties | `{ method() {} }` |
| Block-scoped `let`/`const` | lowered to `var` |

---

## Emitter-native support

These are handled directly by the AOT emitter:

### Variables and functions
- `var`, `let`, `const`
- Function declarations, function expressions, arrow functions
- Closures - nested functions capture free variables from outer scopes
- `arguments` object inside regular functions
- `this` - properly threaded through method calls, constructors, and `apply`/`call`

### Objects
- Object literals `{ key: value }`
- Property get/set via `.` and `[]`
- `__proto__` chain walking for inherited method lookup
- `new` with user-defined constructors - creates object, sets prototype, calls constructor
- `Object.create`, `Object.assign`, `Object.keys`, `Object.setPrototypeOf`, `Object.getPrototypeOf`, `Object.defineProperty`

### Arrays
- Array literals `[1, 2, 3]`
- Index access
- Full array method suite: `push`, `pop`, `shift`, `unshift`, `concat`, `join`, `slice`, `reverse`, `indexOf`, `includes`, `forEach`, `map`, `filter`, `find`, `findIndex`, `some`, `every`, `reduce`, `toString`

### Classes (via Babel lowering)
Babel rewrites `class`/`extends` to prototype-based constructor functions. The emitter fully supports the output:
- Constructors with `this` assignment
- Instance methods on prototype
- Inheritance via `extends` - `super()` calls work via `_Animal.apply(this, arguments)` pattern
- Multiple levels of inheritance

```js
class Animal {
  constructor(name) { this.name = name; }
  speak() { return this.name + " makes a noise."; }
}
class Dog extends Animal {
  speak() { return this.name + " barks."; }
}
const d = new Dog("Rex");
console.log(d.speak()); // Rex barks.
```

### Control flow
`if/else`, `switch/case`, `while`, `do/while`, `for`, `for...in`, `for...of` (via Babel), `try/catch`, `throw`, labeled `break`/`continue`, `finally`

### Operators
- All arithmetic, comparison, logical (`&&`, `||`, `??`), bitwise operators
- Ternary `?:`
- `typeof`, `instanceof`, `in`, `void`, `delete`
- Assignment operators (`+=`, `-=`, `&&=`, `||=`, `??=`, etc.)

### Strings
- Concatenation, `+` coercion
- String methods: `length`, `indexOf`, `includes`, `startsWith`, `endsWith`, `slice`, `substring`, `toUpperCase`, `toLowerCase`, `trim`, `split`, `replace`, `charAt`, `charCodeAt`

### Globals
| Global | Available methods / properties |
|--------|-------------------------------|
| `console` | `log`, `error`, `warn`, `info` |
| `Math` | Full `Math.*` suite |
| `JSON` | `parse`, `stringify` |
| `Object` | `create`, `assign`, `keys`, `setPrototypeOf`, `getPrototypeOf`, `defineProperty` |
| `process` | `argv`, `env`, `exit`, `cwd`, `pid`, `platform` |

### Node.js built-in shims

| Module | Supported |
|--------|-----------|
| `fs` | `readFileSync`, `writeFileSync`, `appendFileSync`, `existsSync`, `readdirSync` |
| `path` | `join`, `resolve`, `dirname`, `basename`, `extname` |
| `os` | `platform`, `homedir`, `tmpdir` |
| `http` | **Island only** - `createServer` is not available in AOT. Use `src/island.js`. |
| `process` | see Globals above |

---

## Not supported in strict AOT

| Feature | Workaround |
|---------|-----------|
| `async`/`await`, `Promise` | Use the QuickJS island |
| Generators (`function*`) | Use the QuickJS island |
| `eval` / `new Function(str)` | Use the QuickJS island |
| Dynamic `require(expr)` | Use static `require("./path")` or island |
| Regular expressions (`/pattern/`) | Use island or pre-compute patterns |
| ES module `import`/`export` | Use CommonJS `require()`/`module.exports` |
| `WeakMap`, `WeakRef`, `Symbol` | Use the QuickJS island |
| `Proxy` / `Reflect` | Use the QuickJS island |
| Full `String.prototype` | Uncommon methods fall through at runtime |
| `Array` / `Map` / `Set` constructors | Use object literals and arrays directly |

---

## Strict mode enforcement

By default, unrecognised features are silently skipped. To turn them into hard errors:

```bash
export BLUE_STRICT_UNSUPPORTED=1
```

---

## Recommendations

1. **Prefer AOT for control flow, math, and native APIs** - it compiles to tight C++ with no interpreter overhead.
2. **Use classes freely** - Babel handles lowering; the emitter fully supports the output including inheritance.
3. **Reach for the island for async/npm** - if you need `await`, npm packages, or a full HTTP server, put that code in `src/island.js`. Communicate over `Blue.callAot()` / `Blue.callIsland()` with JSON strings.
4. **`--print-c` is your debugger** - run `blue -compile file.js --print-c` to see the generated C++ and understand exactly what the emitter produced.
