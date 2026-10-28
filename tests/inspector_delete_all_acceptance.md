# Inspector Delete All References acceptance

The Inspector's References area exposes **Delete All…** when references exist.
It is disabled when there are no references or pending cleanup. The confirmation
sheet shows the exact count and full ordered IDs captured when it opens.

| Scenario | Expected result | Local evidence |
| --- | --- | --- |
| Empty history | Action is disabled; no sheet opens. | Native action test: PASS |
| Cancel | No reference is deleted. | Native action test: PASS |
| Repeated activation while a sheet is open | The first reviewed set remains authoritative. | Native action test: PASS |
| New reference after sheet opens | Confirmation deletes only the reviewed IDs; the new reference remains. | Native action test: PASS |
| Stale completion callback | No second deletion occurs. | Native action test: PASS |
| Cleanup failure | Inspector reports partial cleanup and offers a retry of the same reviewed set. | Native action test: PASS |
| Successful deletion | Deleted selection and preview are cleared; marks and pointer policy refresh. | Code inspection: PASS; physical UI: NOT_RUN |
| Physical Inspector sheet, accessibility, and local files | Exercise on an isolated disposable profile before release. | Physical UI: NOT_RUN |

Native verification uses `inspector_reference_actions_test` in an isolated CMake
build. It includes the production action state from `overlay_mac.mm` with a
test guard and does not launch the SeeThis app or touch a user reference store.
