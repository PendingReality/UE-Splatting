# Contributing

Contributions are welcome. UESplatting currently targets Unreal Engine 5.8 and
uses the MIT License.

## Setup

1. Open `UESplattingDemo.uproject` with Unreal Engine 5.8.
2. Build the `UESplattingDemoEditor` target.
3. Run the `UESplatting.*` automation tests from Session Frontend.

Before submitting a change:

- Build the affected Editor or Game target.
- Run the relevant automation tests.
- Describe the formats, platform, and user workflow you tested.
- Include a representative image when changing rendering or scene capture.
- Keep generated Unreal folders and unlicensed splat data out of commits.

New source files should retain the repository's SPDX license header. Add
third-party attribution when an implementation incorporates external code or
format definitions.
