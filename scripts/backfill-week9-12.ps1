# One-off: record work already done in weeks 9-12 as closed issues, honestly dated.
# Run from the repo root in PowerShell:  .\scripts\backfill-week9-12.ps1
$ErrorActionPreference = "Stop"
$repo = "PhantomSnow/reactor-core"
$ms   = "Week 9-12"

$existing = gh api "repos/$repo/milestones?state=all" --jq ".[].title"
if ($existing -notcontains $ms) {
  gh api "repos/$repo/milestones" -f "title=$ms" -f "due_on=2026-11-30T15:59:59Z" -f "description=Proposal, repository setup and planning" | Out-Null
}

function New-DoneIssue($title, $labels, $body, $closeNote) {
  $tmp = New-TemporaryFile
  Set-Content -Path $tmp -Value $body -Encoding UTF8
  $url = gh issue create --repo $repo --title $title --label $labels --milestone $ms --body-file $tmp | Select-Object -Last 1
  Remove-Item $tmp
  $num = $url.Split("/")[-1]
  gh issue close $num --repo $repo --reason completed --comment $closeNote | Out-Null
  Write-Host "#$num closed: $title"
}

New-DoneIssue "Submit project proposal" "report" @'
Recorded after the fact: this work was done before issue tracking was set up.

- One-page proposal `Group8_proposal.pdf`: team, category, goal, scope, tools, key risks
- Submitted on the course LMS on 2026-11-04 15:23 (deadline 2026-11-10); graded
'@ "Done on 2026-11-04: Group8_proposal.pdf submitted, 6 days before the deadline."

New-DoneIssue "Create repository and baseline tag" "docs" @'
Recorded after the fact: this work was done before issue tracking was set up.

- Pushed reactor-core to GitHub (commit 87fe38b, 2026-11-04)
- Tagged the pre-course state as `baseline` (2026-11-04). All testing work for the term project is measured against this tag: `git diff baseline`
'@ "Done on 2026-11-04 (commit 87fe38b, tag baseline)."

New-DoneIssue "Plan remaining work as issues and milestones" "docs,report" @'
- Labels: test, fuzz, ci, refactor, docs, report, bug
- Weekly milestones Week 13 to Week 16 with due dates
- Issues #1 to #15 with "Done when" checklists, created by `scripts/setup-github-issues.ps1`
'@ "Done on 2026-11-26: issues #1-#15 and milestones created."

# #1 is finished this week, so it belongs to this milestone.
gh issue edit 1 --repo $repo --milestone $ms | Out-Null
Write-Host "Moved #1 to $ms. Done."
