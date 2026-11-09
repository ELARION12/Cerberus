import unittest
import sys
import os

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '../../python')))

import cerberus_agent
from cerberus_agent import KVCacheArena, VectorBuffer, __version__

class TestV140Features(unittest.TestCase):
    def test_version(self):
        self.assertEqual(__version__, "1.4.0")

    def test_kv_cache_arena(self):
        kv = KVCacheArena(100, "/cerberus_py_test_kv_")
        self.assertEqual(kv.capacity_pages(), 100)
        self.assertEqual(kv.used_pages(), 0)

        sys_prompt = "System prompt context prefix for LLM"
        block = kv.allocate_block(sys_prompt, 4096)
        self.assertIsNotNone(block)
        self.assertEqual(block.page_count, 1)

        hit = kv.lookup_prefix(sys_prompt)
        self.assertIsNotNone(hit)
        self.assertEqual(hit.block_id, block.block_id)

        kv.release_block(block.block_id)
        kv.release_block(block.block_id)
        kv.release_block(block.block_id)
        self.assertEqual(kv.active_blocks(), 0)

    def test_vector_buffer_v140(self):
        vb = VectorBuffer(4)
        vb.enable_sq8(True)
        vb.build_hnsw_index(16, 64, 32)

        self.assertTrue(vb.is_sq8_enabled())
        self.assertTrue(vb.has_hnsw())

        vb.push([1.0, 0.0, 0.0, 0.0], "Chunk 1")
        vb.push([0.0, 1.0, 0.0, 0.0], "Chunk 2")

        matches = vb.query([0.9, 0.1, 0.0, 0.0], 2)
        self.assertGreater(len(matches), 0)
        self.assertEqual(matches[0].text, "Chunk 1")

if __name__ == "__main__":
    unittest.main()
