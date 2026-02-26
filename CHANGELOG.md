# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

- n/a

## [0.2.2] - 2026-02-26

### Changed
- Fixed runtime handling of HCL managers 5-8 (ETS assignments now applied correctly).
- Added global ETS parameters for switch transition behavior (separate ON/OFF transition time).
- Applied switch transition timing consistently in runtime for both HCL and non-HCL switch paths.

## [0.2.1] - 2026-02-25

### Changed
- Added channel-specific HCL lock support (parameters, com objects, runtime behavior).
- Removed duplicated global HCL manager status-KO section in ETS UI.
- Cleared build warning for unused variable in channel lock status handling.

## [0.2.0] - 2026-02-24

### Changed
- Start of the 0.2.x development stream for room/zone support.
- Module version raised from 0.1.0 to 0.2.0.

## [0.1.0] - 2026-02-03

### Added
- Project initialization
- Basic module structure

