#!/usr/bin/env bash
# ─── create-project-board.sh ─────────────────────────────────────────────────
# Make (or update) the clearCore GitHub Project board with the GitHub CLI.
#
# Before the first run, give the CLI token the project scope:
#
#     gh auth refresh -s project
#
# Then run the script from any directory:
#
#     scripts/github/create-project-board.sh            # make or update the board
#     scripts/github/create-project-board.sh --dry-run  # show the plan; change nothing
#
# The script is idempotent. You can run it again at any time:
#   - It uses the project with the same title if one exists.
#   - It links the repository to the project only one time.
#   - It adds each open issue that is not on the board.
#   - It sets a field only when the field is empty, so manual changes stay.
#
# Field values come from project-board-items.tsv (next to this script). For an
# issue that the file does not list, the script sets Status to Todo and gets
# the priority from the labels. Area and Size stay empty.
#
# Environment (all optional):
#   OWNER       Project owner (default: khenderson20)
#   REPO        Repository as owner/name (default: khenderson20/clearCore)
#   TITLE       Project title (default: clearCore Roadmap)
#   ITEMS       Field-value file (default: project-board-items.tsv)
#   VISIBILITY  PUBLIC or PRIVATE; unset keeps the current visibility
#   GH          gh executable (default: gh)
#
# Needs bash 4 or later (macOS: brew install bash) and gh 2.31 or later.
# The GitHub API cannot make project views, so the script tells you the two
# manual steps for the Board view at the end.
set -euo pipefail

die() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}
log() { printf '%s\n' "$*"; }

((BASH_VERSINFO[0] >= 4)) || die "bash 4 or later is necessary (this is bash $BASH_VERSION)"

OWNER="${OWNER:-khenderson20}"
REPO="${REPO:-khenderson20/clearCore}"
TITLE="${TITLE:-clearCore Roadmap}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ITEMS="${ITEMS:-$HERE/project-board-items.tsv}"
VISIBILITY="${VISIBILITY:-}"
GH="${GH:-gh}"

DRY_RUN=0
case "${1:-}" in
    --dry-run) DRY_RUN=1 ;;
    "") ;;
    *) die "usage: $0 [--dry-run]" ;;
esac

# Single-select fields that the script makes. The built-in Status field
# (Todo / In Progress / Done) exists in each new project.
declare -A FIELD_OPTIONS=(
    [Priority]="P0,P1,P2,P3"
    [Area]="Core,Pipeline,Assembler,TUI,Qt GUI,GDB stub,RISC-V,Branch prediction,CI / Build"
    [Size]="XS,S,M,L,XL"
)
FIELDS=(Status Priority Area Size)

# Tab is an IFS whitespace character, so `read` merges two tabs and loses an
# empty column. Every column therefore holds a value, and "-" means "none".
none() { [[ -z "$1" || "$1" == "-" ]]; }

# ─── Preconditions ───────────────────────────────────────────────────────────
command -v "$GH" > /dev/null || die "the GitHub CLI ($GH) is not installed: https://cli.github.com"
[[ -r "$ITEMS" ]] || die "cannot read $ITEMS"
auth="$("$GH" auth status 2>&1)" || die "gh is not logged in. Run: gh auth login"
# gh lists the scopes of an OAuth token only. With no list, let the API decide.
if grep -q "Token scopes:" <<< "$auth" && ! grep -q "'project'" <<< "$auth"; then
    die "the gh token has no 'project' scope. Run: gh auth refresh -s project"
fi

# ─── Wanted field values: the TSV file, then the labels ──────────────────────
declare -A WANT # "<issue>|<field>" -> option name
while IFS=$'\t' read -r issue status priority area size _; do
    [[ -z "$issue" || "$issue" == \#* ]] && continue
    [[ "$issue" =~ ^[0-9]+$ ]] || die "$ITEMS: bad issue number '$issue'"
    [[ -n "${size:-}" ]] || die "$ITEMS: issue $issue needs 5 columns (use - for none)"
    WANT["$issue|Status"]="$status"
    WANT["$issue|Priority"]="$priority"
    WANT["$issue|Area"]="$area"
    WANT["$issue|Size"]="$size"
done < "$ITEMS"

priority_from_labels() {
    case ",$1," in
        *,security,* | *,bug,*) echo P1 ;;
        *,feature,* | *,enhancement,*) echo P2 ;;
        *) echo P3 ;;
    esac
}

# Capture each query in a variable first: a failure inside a process
# substitution does not stop the script under `set -e`.
issues_tsv="$("$GH" issue list --repo "$REPO" --state open --limit 1000 \
    --json number,url,labels \
    --jq '.[] | [(.number | tostring), .url, ([.labels[].name] | join(",") | if . == "" then "-" else . end)] | @tsv')"
OPEN_ISSUES=()
[[ -n "$issues_tsv" ]] && mapfile -t OPEN_ISSUES <<< "$issues_tsv"
log "Open issues in $REPO: ${#OPEN_ISSUES[@]}"

for row in "${OPEN_ISSUES[@]}"; do
    IFS=$'\t' read -r issue _ labels <<< "$row"
    [[ -n "${WANT["$issue|Status"]+set}" ]] && continue
    WANT["$issue|Status"]="Todo"
    WANT["$issue|Priority"]="$(priority_from_labels "$labels")"
    WANT["$issue|Area"]="-"
    WANT["$issue|Size"]="-"
done

if ((DRY_RUN)); then
    log "Dry run. The board '$TITLE' of $OWNER gets these values (in empty fields only):"
    printf '%-7s %-12s %-9s %-18s %s\n' ISSUE STATUS PRIORITY AREA SIZE
    for row in "${OPEN_ISSUES[@]}"; do
        IFS=$'\t' read -r issue _ _ <<< "$row"
        printf '#%-6s %-12s %-9s %-18s %s\n' "$issue" "${WANT["$issue|Status"]}" \
            "${WANT["$issue|Priority"]}" "${WANT["$issue|Area"]}" "${WANT["$issue|Size"]}"
    done
    exit 0
fi

# ─── Project ─────────────────────────────────────────────────────────────────
number="$("$GH" project list --owner "$OWNER" --limit 1000 --format json \
    --jq '.projects[] | [(.number | tostring), .title] | @tsv' |
    awk -F'\t' -v t="$TITLE" '$2 == t { print $1; exit }')"
if [[ -z "$number" ]]; then
    number="$("$GH" project create --owner "$OWNER" --title "$TITLE" --format json --jq .number)"
    log "Made project #$number: $TITLE"
else
    log "Uses project #$number: $TITLE"
fi
project_tsv="$("$GH" project view "$number" --owner "$OWNER" --format json --jq '[.id, .url] | @tsv')"
IFS=$'\t' read -r project_id project_url <<< "$project_tsv"

"$GH" project edit "$number" --owner "$OWNER" \
    --description "Roadmap and work items of $REPO" \
    --readme "$(
        cat << 'README'
Roadmap and work items of clearCore.

- **Status**: Todo, In Progress (a pull request is open), Done.
- **Priority**: P0 (do now) to P3 (low).
- **Area**: the part of clearCore that the work changes.
- **Size**: XS to XL, the relative effort.

`scripts/github/create-project-board.sh` makes and updates this project. Edit
`scripts/github/project-board-items.tsv` to change the start values.
README
    )" > /dev/null
if [[ -n "$VISIBILITY" ]]; then
    "$GH" project edit "$number" --owner "$OWNER" --visibility "$VISIBILITY" > /dev/null
fi

# Link the repository one time. The CLI has no query for linked repositories,
# so ask the GraphQL API.
# shellcheck disable=SC2016 # $id, $f and $endCursor are GraphQL and jq variables, not shell ones.
linked="$("$GH" api graphql -f query='
    query($id: ID!) {
      node(id: $id) { ... on ProjectV2 { repositories(first: 100) { nodes { nameWithOwner } } } }
    }' -f id="$project_id" --jq '.data.node.repositories.nodes[].nameWithOwner')"
if grep -qixF "$REPO" <<< "$linked"; then
    log "Repository $REPO is linked."
else
    "$GH" project link "$number" --owner "$OWNER" --repo "${REPO#*/}"
    log "Linked repository $REPO."
fi

# ─── Fields and options ──────────────────────────────────────────────────────
# One row for each option: field name, field id, option name, option id.
field_rows() {
    # shellcheck disable=SC2016 # $f is a jq variable, not a shell one.
    "$GH" project field-list "$number" --owner "$OWNER" --limit 100 --format json \
        --jq '.fields[] | . as $f | (.options // [])[] | [$f.name, $f.id, .name, .id] | @tsv'
}
declare -A FIELD_ID OPTION_ID # FIELD_ID[field]; OPTION_ID["field|option"]
load_fields() {
    local rows
    rows="$(field_rows)"
    FIELD_ID=()
    OPTION_ID=()
    while IFS=$'\t' read -r field field_id option option_id; do
        [[ -n "$field" ]] || continue
        FIELD_ID[$field]="$field_id"
        OPTION_ID["$field|$option"]="$option_id"
    done <<< "$rows"
}
load_fields
for field in Priority Area Size; do
    if [[ -z "${FIELD_ID[$field]:-}" ]]; then
        "$GH" project field-create "$number" --owner "$OWNER" --name "$field" \
            --data-type SINGLE_SELECT --single-select-options "${FIELD_OPTIONS[$field]}" > /dev/null
        log "Made field $field."
    fi
done
load_fields
for field in "${FIELDS[@]}"; do
    [[ -n "${FIELD_ID[$field]:-}" ]] || die "the project has no single-select field '$field'"
done

# ─── Items on the board now, with their single-select values ─────────────────
declare -A ITEM_ID CURRENT # ITEM_ID[url]; CURRENT["url|field"] -> option name
# shellcheck disable=SC2016 # $id, $f and $endCursor are GraphQL and jq variables, not shell ones.
items_tsv="$("$GH" api graphql --paginate -f query='
    query($id: ID!, $endCursor: String) {
      node(id: $id) {
        ... on ProjectV2 {
          items(first: 100, after: $endCursor) {
            pageInfo { hasNextPage endCursor }
            nodes {
              id
              content { ... on Issue { url } ... on PullRequest { url } }
              fieldValues(first: 20) {
                nodes {
                  ... on ProjectV2ItemFieldSingleSelectValue {
                    name
                    field { ... on ProjectV2SingleSelectField { name } }
                  }
                }
              }
            }
          }
        }
      }
    }' -f id="$project_id" --jq '.data.node.items.nodes[]
        | [(.content.url // "-"), .id,
           ([.fieldValues.nodes[] | select(.field.name != null) | "\(.field.name)=\(.name)"]
            | join(";") | if . == "" then "-" else . end)]
        | @tsv')"
while IFS=$'\t' read -r url item values; do
    [[ -n "$url" ]] || continue
    none "$url" && continue # a draft item has no URL
    ITEM_ID[$url]="$item"
    none "$values" && continue
    IFS=';' read -r -a pairs <<< "$values"
    for pair in "${pairs[@]}"; do
        CURRENT["$url|${pair%%=*}"]="${pair#*=}"
    done
done <<< "$items_tsv"

# ─── Add the open issues and fill the empty fields ───────────────────────────
added=0
set_count=0
for row in "${OPEN_ISSUES[@]}"; do
    IFS=$'\t' read -r issue url _ <<< "$row"
    item="${ITEM_ID[$url]:-}"
    if [[ -z "$item" ]]; then
        item="$("$GH" project item-add "$number" --owner "$OWNER" --url "$url" --format json --jq .id)"
        ITEM_ID[$url]="$item"
        added=$((added + 1))
    fi
    for field in "${FIELDS[@]}"; do
        want="${WANT["$issue|$field"]:--}"
        none "$want" && continue
        [[ -n "${CURRENT["$url|$field"]:-}" ]] && continue # keep a manual value
        option="${OPTION_ID["$field|$want"]:-}"
        if [[ -z "$option" ]]; then
            log "warning: field $field has no option '$want' (issue #$issue). Add the option on the board."
            continue
        fi
        "$GH" project item-edit --id "$item" --project-id "$project_id" \
            --field-id "${FIELD_ID[$field]}" --single-select-option-id "$option" > /dev/null
        set_count=$((set_count + 1))
    done
done

log "Added $added issues and set $set_count field values."
log ""
log "Board: $project_url"
log "The GitHub API cannot make views. Do these two steps one time on the board:"
log "  1. Open the menu of 'View 1'. Set Layout to Board, and set Column by to Status."
log "  2. Add a Table view. Set Group by to Area, and set Sort by to Priority."
