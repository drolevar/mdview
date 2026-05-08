# Syntax highlighting

C++:

```cpp
#include <iostream>
int main() {
    std::cout << "hi" << std::endl;
}
```

Python:

```python
def greet(name):
    return f"Hello, {name}!"
```

JSON:

```json
{
    "name": "mdview",
    "version": "0.4.0"
}
```

YAML:

```yaml
plugins:
  - mdview
  - example
```

Shell:

```sh
ls -la /tmp
```

Unknown language (should fall back to plaintext):

```brainfuck
+++++++++[>++++++++<-]>.
```
