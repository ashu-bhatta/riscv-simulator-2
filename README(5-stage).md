The following commands have been added as part of the in-house simlator extension:

- `modify_config` or `mconfig`: `Section`, `Key`, `Value`
  - Modifies the internal configuration by setting the specified key in the given section to the provided value.
  - `Execution`
    - `processor_type` (string) : `single_stage` | `multi_stage`  | `multi_stage_with_forwarding` | `multi_stage_with_hazard_detection` | `multi_stage_with_both`
  - `Cache`
    - `enabled` : `true` | `false`
    - `capacity` : (in Bytes)
    - `block_size` : (Bytes per line)
    - `associativity` : (integer)
    - `replacement_policy` : `LRU`
    - `write_hit_policy` : `WriteBack` | `WriteThrough`
    - `write_miss_policy` : `NoWriteAllocate` | `WriteAllocate`  


Follow the standard steps that was used to run the origin in house simulator , except change to one of the following configurations



