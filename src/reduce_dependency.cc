#include "reduce_dependency.h"

#include <cmath>

#include "common/config.h"
#include "util/logging.h"

// maximum_chain_length = object_size / (HOPLITE_BANDWIDTH * HOPLITE_RPC_LATENCY);

ReduceTreeChain::ReduceTreeChain(int64_t object_count, int64_t maximum_chain_length)
    : object_count_(object_count), maximum_chain_length_(maximum_chain_length) {
  int64_t k = maximum_chain_length;
  // 2^d -1 + 2^(d-1)*2k = object_count
  int d = int(ceil(log2(double(object_count + 1) / double(k + 1))));
  int max_depth = int(floor(log2(double(object_count + 1))));
  depth_ = d < max_depth ? d : max_depth;
  if (depth_ <= 0) {
    // In this case we are totally a chain.
    depth_ = 0;
    chains_.emplace_back();
    chains_[0].resize(object_count);
  } else {
    // the tree is always a full binary tree
    tree_.resize((1LL << depth_) - 1);
    int64_t remaining_size = object_count - tree_.size();
    DCHECK(remaining_size >= 0);
    chains_.resize(1LL << depth_);
    for (size_t i = 0; i < chains_.size(); ++i) {
      // at most differ by 1
      chains_[i].resize(remaining_size / chains_.size() + int(i < remaining_size % chains_.size()));
    }
  }

  // initialize chains
  for (auto &chain : chains_) {
    if (chain.size() > 0) {
      chain[0].is_tree_node = false;
      chain[0].subtree_size = 1;
      for (int i = 1; i < chain.size(); i++) {
        chain[i].is_tree_node = false;
        chain[i].subtree_size = chain[i - 1].subtree_size + 1;
        chain[i].left_child = &chain[i - 1];
        chain[i - 1].parent = &chain[i];
      }
    }
  }

  // initialize tree
  if (depth_ > 0) {
    // initalize the bottom of the tree
    int w = depth_ - 1;
    int front = (1 << w) - 1;
    int end = (1 << (w + 1)) - 1;
    if (chains_.size() > 0) {
      for (int i = front; i < end; i++) {
        auto &t = tree_[i];
        t.is_tree_node = true;
        t.subtree_size = 1;
        auto &left_chain = chains_[(i - front) << 1];
        auto &right_chain = chains_[((i - front) << 1) + 1];
        if (left_chain.size() > 0) {
          t.left_child = &left_chain.back();
          t.left_child->parent = &t;
          t.subtree_size += t.left_child->subtree_size;
        }
        if (right_chain.size() > 0) {
          t.right_child = &right_chain.back();
          t.right_child->parent = &t;
          t.subtree_size += t.right_child->subtree_size;
        }
      }
    } else {
      for (int i = front; i < end; i++) {
        tree_[i].is_tree_node = true;
        tree_[i].subtree_size = 1;
      }
    }

    if (depth_ > 1) {
      // initalize the upper part
      for (int w = depth_ - 2; w >= 0; w--) {
        int front = (1 << w) - 1;
        int end = (1 << (w + 1)) - 1;
        for (int i = front; i < end; i++) {
          auto &t = tree_[i];
          t.is_tree_node = true;
          t.left_child = &tree_[(i << 1) + 1];
          t.right_child = &tree_[(i << 1) + 2];
          t.left_child->parent = &t;
          t.right_child->parent = &t;
          t.subtree_size = 1 + t.left_child->subtree_size + t.right_child->subtree_size;
        }
      }
    }
  }

  // assign orders by in-order tree traverse
  map_.resize(object_count);
  for (int i = 0; i < tree_.size(); i++) {
    auto &t = tree_[i];
    map_[t.init_order()] = &t;
  }
  for (auto &c : chains_) {
    if (c.size() > 0) {
      for (Node *s = &c.back(); s != NULL; s = s->left_child) {
        map_[s->init_order()] = s;
      }
    }
  }
}

std::string ReduceTreeChain::DebugString() {
  std::stringstream s;
  s << std::endl << "==============================================================" << std::endl;
  s << "object_count: " << object_count_ << ", maximum_chain_length: " << maximum_chain_length_ << std::endl;
  s << std::endl;
  if (chains_.size() == 0) {
    s << "Chain: NULL";
  } else {
    for (int i = 0; i < chains_.size(); i++) {
      auto const &c = chains_[i];
      s << "Chain #" << i << ", length=" << c.size() << ": [ ";
      for (auto const &y : c) {
        if (y.parent) {
          s << y.order << "->" << y.parent->order << " ";
        } else {
          s << y.order << " ";
        }
      }
      s << "]" << std::endl;
    }
  }
  s << std::endl << std::endl;
  if (tree_.size() == 0) {
    s << "Tree: NULL" << std::endl;
  } else {
    s << "Tree:" << std::endl;
    for (int i = 0; i < tree_.size(); i++) {
      auto &y = tree_[i];
      if (y.parent) {
        s << y.order << "->" << y.parent->order << " ";
      } else {
        s << y.order << " ";
      }
      if (((i + 2) & (i + 1)) == 0) {
        s << std::endl;
      }
    }
  }
  s << "==============================================================" << std::endl;
  return s.str();
}
