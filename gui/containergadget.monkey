Strict



'temp
Import mojo
Import gui



Class ContainerGadget Extends ViewGadget
	Field children := New List< Gadget >
	
	Method New( x:Int, y:Int, w:Int, h:Int )
		Self.x = x
		Self.y = y
		Self.w = w
		Self.h = h
	End
		
	Method AddChild:Void( child:Gadget )
		children.AddLast( child )
		child.parent = Self
		child.window = window
	 	Local container:ContainerGadget = ContainerGadget( child )
		If container <> Null Then container._PassDownWindow()
	End
	
	Method _PassDownWindow:Void()
		For Local child:Gadget = EachIn children
			child.window = window
			Local container:ContainerGadget = ContainerGadget( child )
			If container <> Null container._PassDownWindow()
		Next
	End
		
	Method HandleEvent:Gadget( event:Event )
		If event.destination = Self
			Return Self
		EndIf
			
		If event.destination <> Null
			event.x = event.destination.GetLocalX( event.x, Self )
			event.y = event.destination.GetLocalY( event.y, Self )
			Return event.destination.HandleEvent( event )
		Else
			Local champ:Gadget
				
			For Local child:Gadget = EachIn children.Backwards()
				If Not child.enabled Then Continue
				
				If RectangleContainsPoint( child.x, child.y, child.w, child.h, event.x, event.y )
					champ = child
					Exit
				EndIf
			Next
				
			If champ <> Null
				event.x = champ.GetLocalX( event.x, Self )
				event.y = champ.GetLocalY( event.y, Self )
				event.destination = champ.HandleEvent( event )
			EndIf
		EndIf
			
		Local out:Gadget = event.destination
			
		If out = Null
			out = Self
		EndIf
			
		Return out
	End
		
	Method OnRender:Void()
		
	End
		
	Method OnRenderInterior:Void()
		PushMatrix()
		PushScissor()
		
		Local _o:Float[] = GetMatrix()
		Local ox:Int = _o[4], oy:Int = _o[5]
		Local _s:Float[] = GetScissor()
		Local sx:Int = _s[0], sy:Int = _s[1], sw:Int = _s[2], sh:Int = _s[3]
			
		For Local child:Gadget = EachIn children
			If Not child.enabled Then Continue
			PushMatrix()
			PushScissor()
			Local cx:Int = child.x + ox, cy:Int = child.y + oy, cw:Int = child.w, ch:Int = child.h
			Local s:Int[] = RectangleUnion( sx, sy, sw, sh, cx, cy, cw, ch )
			SetScissor s[0], s[1], s[2], s[3]
			Translate child.x, child.y
			child.Render()
			PopMatrix()
			PopScissor()
		Next
			
		PopMatrix()
		PopScissor()
	End
End
