/*!
 * Offline Chromium reader foundation: narrow TypeScript format ports and source references.
 * These references are not dependencies or a claim of complete Chromium support.
 *
 * cclgroupltd/ccl_chromium_reader
 * https://github.com/cclgroupltd/ccl_chromium_reader/tree/ad37443541dd91dac3ac1fb2eecaeb8693f36815
 * Upstream root LICENSE: Copyright 2020, CCL Forensics
 * storage_formats/ccl_leveldb.py: Copyright 2020-2021, CCL Forensics
 *   -> leveldb.ts (bounded byte parsing only; strict live-state/CRC rules added here).
 * ccl_chromium_localstorage.py: Copyright 2021-2024, CCL Forensics
 * ccl_chromium_sessionstorage.py: Copyright 2021, CCL Forensics
 *   -> storage.ts (string/namespace formats; host snapshots/ownership rules added here).
 * ccl_chromium_indexeddb.py: Copyright 2020-2024, CCL Forensics
 *   -> indexeddb.ts (key format subset; strict current metadata/comparator added from Chromium below).
 * serialization_formats/ccl_v8_value_deserializer.py: Copyright 2020, CCL Forensics
 * serialization_formats/ccl_blink_value_deserializer.py: Copyright 2020, CCL Forensics
 *   -> values.ts (bounded V8 value subset; pinned Chromium/V8 sources below extend envelopes/host references).
 *
 * cclgroupltd/ccl_simplesnappy
 * https://github.com/cclgroupltd/ccl_simplesnappy/tree/3d085230baa8c46cf2090ebba29bf6e8eab31087
 * Copyright 2024, CCL Forensics
 *   -> snappy.ts (bounded raw Snappy format port).
 *
 * The following MIT terms apply to all the referenced/ported CCL source portions:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * This source module is included by desktop/tsconfig.json. Its notice is retained
 * by that configuration's normal emit and out/** packaging, without a runtime
 * import or third-party executable. No emitted build was made for this candidate.
 */
export {};

/*!
 * Chromium format/comparator/scopes/external-object port provenance:
 * Electron tag v45.2.1 -> f81ca3d4f09f57667238120adacb01239d5ecd89.
 * That commit's DEPS pins Chromium156.0.8078.75 ->
 * 861389936bf33ea77529ee5dcc0605e68f6bb620 (pinned before this port).
 * Electron DEPS SHA256576898234b34b92bd7386b54ae005407a67132d32cfb115a6643b3b6eb66397d.
 * Sources and original notices:
 * content/browser/indexed_db/indexed_db_leveldb_coding.{cc,h},
 * instance/leveldb/backing_store.cc, storage/common/database/database_identifier.cc:
 * Copyright 2013 The Chromium Authors
 * instance/leveldb/indexed_db_leveldb_operations.cc, Blink IDB value wrapping:
 * Copyright 2017 The Chromium Authors
 * components/services/storage/indexed_db/scopes/{leveldb_scopes_coding,
 * leveldb_scopes}.cc, scopes_metadata.proto:
 * Copyright 2018 The Chromium Authors
 * content/browser/indexed_db/indexed_db_external_object_storage.{cc,h}:
 * Copyright 2019 The Chromium Authors
 * content/browser/indexed_db/file_path_util.cc:
 * Copyright 2024 The Chromium Authors
 * Blink serialization_tag.h:
 * Copyright 2014 The Chromium Authors
 * Blink v8_script_value_{serializer,deserializer}.cc:
 * Copyright 2016 The Chromium Authors
 * Blink trailer_reader.{cc,h}:
 * Copyright 2022 The Chromium Authors
 * Chromium root LICENSE: Copyright 2015 The Chromium Authors
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *    * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *    * Neither the name of Google LLC nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (C) 2009, 2010 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*!
 * V8 source dependency pinned by the above Chromium DEPS:
 * 235050a0df6db2c9d92a419af2b72fbf3b2dd73b, src/objects/value-serializer.{cc,h}.
 * Copyright 2016 the V8 project authors. All rights reserved.
 * The preceding Blink header notice applies to serialized_script_value.h.
 * The following V8 root license applies to the ported V8 format portions.
 *
 * Copyright 2014, the V8 project authors. All rights reserved.
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of Google Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
