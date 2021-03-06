/*
 * Copyright 2011, Blender Foundation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Contributor: 
 *		Jeroen Bakker 
 *		Monique Dewanchand
 */

class ExecutionGroup;

#ifndef _COM_ExecutionSystem_h
#define _COM_ExecutionSystem_h

#include "DNA_color_types.h"
#include "DNA_node_types.h"
#include <vector>
#include "COM_Node.h"
#include "COM_SocketConnection.h"
#include "BKE_text.h"
#include "COM_ExecutionGroup.h"
#include "COM_NodeOperation.h"

using namespace std;

/**
 * @page execution Execution model
 * In order to get to an efficient model for execution, several steps are being done. these steps are explained below.
 *
 * @section EM_Step1 Step 1: translating blender node system to the new compsitor system
 * Blenders node structure is based on C structs (DNA). These structs are not efficient in the new architecture. We want to use classes in order to simplify the system.
 * during this step the blender node_tree is evaluated and converted to a CPP node system.
 *
 * @see ExecutionSystem
 * @see Converter.convert
 * @see Node
 *
 * @section EM_Step2 Step2: translating nodes to operations
 * Ungrouping the GroupNodes. Group nodes are node_tree's in node_tree's. The new system only supports a single level of node_tree. We will 'flatten' the system in a single level.
 * @see GroupNode
 * @see ExecutionSystemHelper.ungroup
 *
 * Every node has the ability to convert itself to operations. The node itself is responsible to create a correct NodeOperation setup based on its internal settings.
 * Most Node only need to convert it to its NodeOperation. Like a ColorToBWNode doesn't check anything, but replaces itself with a ConvertColorToBWOperation.
 * More complex nodes can use different NodeOperation based on settings; like MixNode. based on the selected Mixtype a different operation will be used.
 * for more information see the page about creating new Nodes. [@subpage newnode]
 *
 * @see ExecutionSystem.convertToOperations
 * @see Node.convertToOperations
 * @see NodeOperation base class for all operations in the system
 *
 * @section EM_Step3 Step3: add additional conversions to the operation system
 *   - Data type conversions: the system has 3 data types COM_DT_VALUE, COM_DT_VECTOR, COM_DT_COLOR. The user can connect a Value socket to a color socket. As values are ordered differently than colors a conversion happens.
 *
 *   - Image size conversions: the system can automatically convert when resolutions do not match. An InputSocket has a resize mode. This can be any of the following settings.
 *     - [@ref InputSocketResizeMode.COM_SC_CENTER]: The center of both images are aligned
 *     - [@ref InputSocketResizeMode.COM_SC_FIT_WIDTH]: The width of both images are aligned
 *     - [@ref InputSocketResizeMode.COM_SC_FIT_HEIGHT]: the height of both images are aligned
 *     - [@ref InputSocketResizeMode.COM_SC_FIT]: The width, or the height of both images are aligned to make sure that it fits.
 *     - [@ref InputSocketResizeMode.COM_SC_STRETCH]: The width and the height of both images are aligned
 *     - [@ref InputSocketResizeMode.COM_SC_NO_RESIZE]: bottom left of the images are aligned.
 *
 * @see Converter.convertDataType Datatype conversions
 * @see Converter.convertResolution Image size conversions
 *
 * @section EM_Step4 Step4: group operations in executions groups
 * ExecutionGroup are groups of operations that are calculated as being one bigger operation. All operations will be part of an ExecutionGroup.
 * Complex nodes will be added to separate groups. Between ExecutionGroup's the data will be stored in MemoryBuffers. ReadBufferOperations and WriteBufferOperations are added where needed.
 *
 * <pre>
 *
 *        +------------------------------+      +----------------+
 *        | ExecutionGroup A             |      |ExecutionGroup B|   ExecutionGroup
 *        | +----------+     +----------+|      |+----------+    |
 *   /----->| Operation|---->| Operation|-\ /--->| Operation|-\  |   NodeOperation
 *   |    | | A        |     | B        ||| |   || C        | |  |
 *   |    | | cFFA     |  /->| cFFA     ||| |   || cFFA     | |  |
 *   |    | +----------+  |  +----------+|| |   |+----------+ |  |
 *   |    +---------------|--------------+v |   +-------------v--+
 * +-*----+           +---*--+         +--*-*--+           +--*----+
 * |inputA|           |inputB|         |outputA|           |outputB| MemoryBuffer
 * |cFAA  |           |cFAA  |         |cFAA   |           |cFAA   |
 * +------+           +------+         +-------+           +-------+
 * </pre>
 * @see ExecutionSystem.groupOperations method doing this step
 * @see ExecutionSystem.addReadWriteBufferOperations
 * @see NodeOperation.isComplex
 * @see ExecutionGroup class representing the ExecutionGroup
 */

/**
 * @brief the ExecutionSystem contains the whole compositor tree.
 */
class ExecutionSystem {
private:
	/**
	 * @brief the context used during execution
	 */
	CompositorContext m_context;

	/**
	 * @brief vector of nodes
	 */
	vector<Node *> m_nodes;

	/**
	 * @brief vector of operations
	 */
	vector<NodeOperation *> m_operations;

	/**
	 * @brief vector of groups
	 */
	vector<ExecutionGroup *> m_groups;

	/**
	 * @brief vector of connections
	 */
	vector<SocketConnection *> m_connections;

private: //methods
	/**
	 * @brief add ReadBufferOperation and WriteBufferOperation around an operation
	 * @param operation the operation to add the bufferoperations around.
	 */
	void addReadWriteBufferOperations(NodeOperation *operation);


	/**
	 * find all execution group with output nodes
	 */
	void findOutputExecutionGroup(vector<ExecutionGroup *> *result, CompositorPriority priority) const;
	
	/**
	 * find all execution group with output nodes
	 */
	void findOutputExecutionGroup(vector<ExecutionGroup *> *result) const;

public:
	/**
	 * @brief Create a new ExecutionSystem and initialize it with the
	 * editingtree.
	 *
	 * @param editingtree [bNodeTree *]
	 * @param rendering [true false]
	 */
	ExecutionSystem(RenderData *rd, bNodeTree *editingtree, bool rendering, bool fastcalculation,
	                const ColorManagedViewSettings *viewSettings, const ColorManagedDisplaySettings *displaySettings);

	/**
	 * Destructor
	 */
	~ExecutionSystem();


	/**
	 * @brief execute this system
	 *  - initialize the NodeOperation's and ExecutionGroup's
	 *  - schedule the output ExecutionGroup's based on their priority
	 *  - deinitialize the ExecutionGroup's and NodeOperation's
	 */
	void execute();

	/**
	 * @brief Add an operation to the operation list
	 *
	 * @param operation the operation to add
	 */
	void addOperation(NodeOperation *operation);

	/**
	 * Add an editor link to the system. convert it to an socketconnection (CPP-representative)
	 * this converted socket is returned.
	 */
	SocketConnection *addNodeLink(bNodeLink *bNodeLink);
	void addSocketConnection(SocketConnection *connection);

	/**
	 * Remove a socket connection from the system.
	 */
	void removeSocketConnection(SocketConnection *connection);

	/**
	 * @brief Convert all nodes to operations
	 */
	void convertToOperations();

	/**
	 * @brief group operations in ExecutionGroup's
	 * @see ExecutionGroup
	 */
	void groupOperations();

	/**
	 * @brief get the reference to the compositor context
	 */
	CompositorContext &getContext() { return this->m_context; }

	/**
	 * @brief get the reference to the compositor nodes
	 */
	vector<Node *> &getNodes() { return this->m_nodes; }

	/**
	 * @brief get the reference to the compositor connections
	 */
	vector<SocketConnection *>& getConnections() { return this->m_connections; }

	/**
	 * @brief get the reference to the list of execution groups
	 */
	vector<ExecutionGroup *>& getExecutionGroups() { return this->m_groups; }

	/**
	 * @brief get the reference to the list of operations
	 */
	vector<NodeOperation *>& getOperations() { return this->m_operations; }

private:

	/**
	 * @brief determine the actual data types of all sockets
	 * @param nodes list of nodes or operations to do the data type determination
	 */
	void determineActualSocketDataTypes(vector<NodeBase *> &nodes);
	
	void executeGroups(CompositorPriority priority);

#ifdef WITH_CXX_GUARDEDALLOC
	MEM_CXX_CLASS_ALLOC_FUNCS("COM:ExecutionSystem")
#endif
};

#endif /* _COM_ExecutionSystem_h */
