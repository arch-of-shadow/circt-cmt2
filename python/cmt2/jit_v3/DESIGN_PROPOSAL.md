# JIT v3 API Design Proposal

## Problem Analysis

### v2 Issues
```python
@jit.rule(m, "increment")  # String name
def _(rule):               # def _ boilerplate
    @rule.guard
    def _(g):              # def _ boilerplate  
        g.always()
    @rule.body
    def _(b):              # def _ boilerplate
        val = b.call(count, "read")  # String!
        b.call(count, "write", val + 1)  # String!
```
**Problems**: 3 levels of `def _`, string method names

### v3 Issues  
```python
@jit.rule(m)  # Name inferred
def increment(guard, body):
    guard.always()           # Is this guard or body?
    count.next = count.read + 1  # Mixed together, unclear separation
```
**Problems**: Guard and body operations mixed, unclear semantics

---

## Final Design: Context Managers (SELECTED)

Use `with` statements for explicit, clear regions:

```python
@jit.rule(m)  # Name: "increment" (auto-inferred)
def increment(r):
    with r.guard:     # Clear: following is guard
        r.always()
    
    with r.body:      # Clear: following is body
        count.next = count.read + 1  # Attribute access, no strings!
```

### How it works:
```python
class RuleContext:
    def __init__(self, rule):
        self.guard = GuardContext(self)  # Context manager
        self.body = BodyContext(self)    # Context manager
    
    def always(self): ...  # Works inside guard context
    def equals(self, a, b): ...  # Works inside guard context
```

### Pros:
- ✅ Clear guard/body separation (explicit `with` blocks)
- ✅ No `def _:` boilerplate
- ✅ No string method names
- ✅ Auto-inferred names
- ✅ Pythonic (uses standard `with` statements)
- ✅ Easy to understand

### Cons:
- Slightly more indentation than mixed approach

---

## Alternative Designs Considered

### Option 1: Decorator Chaining (REJECTED - Syntax Error)
```python
@jit.rule(m)
def increment(r):
    @r.guard      # SyntaxError! Decorator must precede a function def
    r.always()
```
Python doesn't allow decorators before statements.

### Option 2: Mixed Guard/Body (REJECTED - User Feedback)
```python
@jit.rule(m)
def increment(guard, body):
    guard.always()           # Unclear: is this guard or body?
    count.next = count.read + 1  # Mixed operations
```
User said: "it's not clear about operations for either guard and body"

---

## Recommendation: Context Managers (IMPLEMENTED)

```python
@jit.elaborate
def counter(width: int = 32):
    circuit = Circuit("Counter")
    
    with jit.module(circuit, "Counter") as m:
        clk = m.clock()
        rst = m.reset()
        count = m.instance(Reg.create(circuit, width), "count", clk=clk, rst=rst)
        
        @jit.rule(m)  # Name extracted from function: "increment"
        def increment(r):
            with r.guard:     # Explicit guard region
                r.always()
            
            with r.body:      # Explicit body region
                count.next = count.read + 1
        
        @jit.value(m, returns=[UInt(width)])  # Name: "get_count"
        def get_count(r):
            with r.guard:
                r.always()
            
            with r.body:
                r.returns(count.read)
    
    return circuit
```

### Line Count Comparison:

| Approach | Lines | Strings | `def _` | Clear Separation |
|----------|-------|---------|---------|------------------|
| v1       | 14    | 3       | 0       | ✅ Yes           |
| v2       | 18    | 3       | 3       | ✅ Yes           |
| v3-mixed | 12    | 0       | 0       | ❌ No            |
| **v3-clean** | **14** | **0** | **0** | **✅ Yes**       |

### Module Context (unchanged):

```python
with jit.module(circuit, "Top") as m:
    clk = m.clock()
    rst = m.reset()
    count = m.instance(Reg.create(circuit, width), "count", ...)
```

This uses `with` statement for module context (which makes sense for resource allocation) and `with r.guard:` / `with r.body:` for rule regions (which makes sense for logic definition with clear phases).
