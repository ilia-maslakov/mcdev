# Kubernetes Panel Plugin Plan

Analogous to the Docker plugin. Backend: `kubectl`.

---

## Navigation Hierarchy

```
k8s:/                              favorites list (saved contexts/clusters)
                                   if list empty: directly shows namespaces of current context
k8s:/<context>/                    namespaces in that context
k8s:/<context>/<ns>/               resource types in namespace
k8s:/<context>/<ns>/pods/          pod list
k8s:/<context>/<ns>/pods/<pod>/    pod details: logs, exec, describe, yaml, containers
k8s:/<context>/<ns>/pods/<pod>/containers/  containers in pod (multi-container)
k8s:/<context>/<ns>/deployments/   deployment list
k8s:/<context>/<ns>/statefulsets/  statefulset list
k8s:/<context>/<ns>/daemonsets/    daemonset list
k8s:/<context>/<ns>/services/      service list
k8s:/<context>/<ns>/configmaps/    configmap list
k8s:/<context>/<ns>/secrets/       secret list (values masked in display)
k8s:/<context>/<ns>/events/        events (read-only, sorted by time)
k8s:/<context>/nodes/              node list (cluster-wide)
```

Root also contains two special files (when not in favorites view):
- `version.txt`      -> `kubectl version`
- `cluster-info.txt` -> `kubectl cluster-info`

---

## Favorites Panel

Shown at `k8s:/` when the plugin opens, analogous to git favorites.

Each entry is a saved kubectl context name. Display name strips leading `contexts/`
prefix so the `type` column `/` does not produce `//contexts/...`.

**Behavior:**
- Enter on a favorite -> switches to that context, shows its namespaces
- `..` / back -> `MC_PPR_CLOSE`
- Footer: `<count> saved contexts`

**Keys (favorites view):**

| Key    | Action                                              |
|--------|-----------------------------------------------------|
| Enter  | Open context (switch kubectl context, show ns list) |
| S-F4   | Add context by name (input dialog)                  |
| F8     | Remove from favorites                               |
| Ctrl-B | Add current context to favorites (from inside view) |
| Ctrl-R | Refresh list                                        |

**Persistence:** `panels.k8s.ini` under `[k8s-favorites]`, key `contexts`.
Same pattern as git: in-memory `GPtrArray *fav_contexts` loaded on open,
saved with `mc_config_del_key` for empty list (avoids the `length==0` skip bug).

---

## Views (k8s_view_t)

```c
K8S_VIEW_FAVORITES          // saved contexts list
K8S_VIEW_NAMESPACES         // namespaces in current context
K8S_VIEW_NODES              // node list (cluster-wide)
K8S_VIEW_RESOURCE_TYPES     // inside a namespace: pods/deploy/svc/...
K8S_VIEW_PODS
K8S_VIEW_POD_DETAILS        // logs / exec / describe / yaml / containers
K8S_VIEW_POD_CONTAINERS
K8S_VIEW_DEPLOYMENTS
K8S_VIEW_STATEFULSETS
K8S_VIEW_DAEMONSETS
K8S_VIEW_SERVICES
K8S_VIEW_CONFIGMAPS
K8S_VIEW_SECRETS
K8S_VIEW_EVENTS
```

---

## Item Kinds (k8s_item_kind_t)

```c
K8S_ITEM_FAVORITE_CONTEXT   // entry in favorites list
K8S_ITEM_NAMESPACE
K8S_ITEM_NODE
K8S_ITEM_RESOURCE_TYPE_DIR  // "pods", "deployments", etc.
K8S_ITEM_POD
K8S_ITEM_POD_DETAIL_DIR     // "logs", "exec", "describe", "yaml", "containers"
K8S_ITEM_CONTAINER
K8S_ITEM_DEPLOYMENT
K8S_ITEM_STATEFULSET
K8S_ITEM_DAEMONSET
K8S_ITEM_SERVICE
K8S_ITEM_CONFIGMAP
K8S_ITEM_SECRET
K8S_ITEM_EVENT
K8S_ITEM_INFO_FILE          // version.txt, cluster-info.txt
```

---

## Columns per View

**Favorites:**
- name (expand), cluster (25), current (1: `*` or ` `)
- format: `"name | cluster:25 | current:1"`

**Pods:**
- name (expand), status (10), restarts (5), age (8), node (20)
- format: `"name | status:10 | restarts:5 | age:8 | node:20"`

**Deployments / StatefulSets / DaemonSets:**
- name (expand), ready (8), age (8)
- format: `"name | ready:8 | age:8"`

**Services:**
- name (expand), type (12), cluster-ip (16), ports (20)

**ConfigMaps / Secrets:**
- name (expand), data-count (6), age (8)

**Nodes:**
- name (expand), status (10), roles (15), age (8), version (14)

**Events:**
- type (8), reason (20), object (30), message (expand)

---

## Key Bindings

| Key    | View         | Action                                   |
|--------|--------------|------------------------------------------|
| Ctrl-R | all          | Refresh (clear cache)                    |
| Ctrl-B | inside ctx   | Add current context to favorites         |
| S-F4   | favorites    | Add context by name (dialog)             |
| F8     | favorites    | Remove from favorites                    |
| F8     | resources    | Delete resource (kubectl delete)         |
| Ctrl-N | inside ctx   | Switch namespace (dialog)                |
| F3     | resources    | View: describe / logs (per item)         |
| S-F3   | resources    | View raw YAML (kubectl get -o yaml)      |
| S-F5   | deploy/sts   | Scale (dialog)                           |
| S-F6   | deploy/sts   | Rollout restart                          |

---

## Plugin Callbacks

| Callback           | Notes                                              |
|--------------------|----------------------------------------------------|
| open               | load favorites; if empty switch to current context |
| close              | free state, cache, favorites list                  |
| get_items          | favorites list OR kubectl commands per view        |
| chdir              | navigate views; favorites Enter sets context       |
| enter              | enter dirs; exec shell for exec detail entry       |
| view               | describe / logs / yaml in mcview                   |
| get_local_copy     | kubectl get -o yaml to temp file                   |
| delete_items       | favorites: remove; resources: kubectl delete       |
| create_item        | favorites: add context; resources: not in MVP      |
| handle_key         | Ctrl-R, Ctrl-B, Ctrl-N                             |
| get_columns        | per-view column sets                               |
| get_column_value   | status/age/roles etc per row                       |
| get_default_format | per-view format strings                            |
| get_title          | breadcrumb: context / ns / resource                |
| get_footer         | favorites: entry count; inside: context + ns       |

---

## kubectl Commands

**Listing:**
```
kubectl config get-contexts -o name
kubectl get namespaces -o custom-columns=...
kubectl get pods        -n <ns> -o custom-columns=...
kubectl get deployments -n <ns> -o custom-columns=...
kubectl get services    -n <ns> -o custom-columns=...
kubectl get configmaps  -n <ns> -o custom-columns=...
kubectl get secrets     -n <ns> -o custom-columns=...
kubectl get nodes           -o custom-columns=...
kubectl get events      -n <ns> --sort-by=.lastTimestamp
```

**Pod operations:**
```
kubectl logs      <pod> -n <ns> [-c <container>] --tail=1000
kubectl exec  -it <pod> -n <ns> [-c <container>] -- sh
kubectl describe pod <pod> -n <ns>
kubectl get pod  <pod> -n <ns> -o yaml
```

**Workload operations:**
```
kubectl scale deployment <name> -n <ns> --replicas=<n>
kubectl rollout restart deployment/<name> -n <ns>
kubectl delete <type> <name> -n <ns>
kubectl get    <type> <name> -n <ns> -o yaml
```

**Context:**
```
kubectl config use-context <ctx>
kubectl config current-context
kubectl version
kubectl cluster-info
```

---

## Source Files

```
src/panel-plugins/k8s/
  k8s.c                  main: open/close/chdir/enter/get_items/handle_key
  k8s-internal.h         enums, structs, shared state
  pods.c                 pod list, logs, exec, describe
  workloads.c            deployments, statefulsets, daemonsets
  services.c             services
  configs.c              configmaps, secrets
  nodes.c                node list
  k8s-cmd.c              kubectl helpers: run_cmd, parse output, cache
  k8s-ui.c               dialogs: ns switch, scale, add context
  k8s_panel.hlp          help file
  Makefile.am
```

---

## Shared State (k8s_data_t)

```c
typedef struct {
    mc_panel_host_t *host;
    k8s_view_t view;
    char *context;                // active kubectl context
    char *namespace;              // active namespace
    char *selected_pod;
    char *selected_resource_type;
    char *title;
    char *help_filename;
    int key_refresh;
    int key_fav_add;              // Ctrl-B: add current context to favorites
    int key_ns_switch;            // Ctrl-N
    GPtrArray *fav_contexts;      // non-NULL in favorites view (list of char*)
    GHashTable *display_to_info;
    GHashTable *cache;            // "<context>/<ns>/<resource>" -> cached output
} k8s_data_t;
```

---

## Favorites Persistence

File: `panels.k8s.ini`, group `[k8s-favorites]`, key `contexts`.
Same pattern as git plugin:
- Load into `fav_contexts` on `open` when no context detected
- `create_item` / Ctrl-B add to `fav_contexts` then save
- `delete_items` (F8) removes from `fav_contexts` then save
- Empty list: use `mc_config_del_key` (not `mc_config_set_string_list` which skips len==0)

---

## Caching Strategy

Per-view string cache keyed by `<context>/<namespace>/<resource>`.
Invalidated on Ctrl-R or TTL (default 30s).
`GHashTable<char*, k8s_cache_entry_t>` inside `k8s_data_t`.

---

## Path Format

`k8s:/<context>/<namespace>/<resource_type>/<resource_name>/<detail>`

Examples:
```
k8s:/                                          favorites
k8s:/prod-cluster/default/pods                 pods in default ns
k8s:/prod-cluster/default/pods/nginx-abc12     pod details
k8s:/prod-cluster/kube-system/deployments      deployments
```

---

## MVP Scope (Phase 1)

1. Favorites panel (open, add via S-F4 and Ctrl-B, remove via F8)
2. Namespaces view, resource types view
3. Pods: list, logs (F3), exec (Enter on exec entry), delete (F8)
4. Deployments: list, describe (F3), delete
5. Services: list, describe (F3)
6. Ctrl-R refresh, Ctrl-N namespace switch
7. Footer: current context + namespace
8. Help file

## Phase 2

9. ConfigMaps, Secrets (masked), Events
10. StatefulSets, DaemonSets
11. Scale dialog (S-F5), rollout restart (S-F6)
12. Nodes view
13. Raw YAML view (S-F3)
14. Cache with TTL
15. Tests: domain parsing, path restoration, favorites persistence
