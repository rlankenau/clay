Strict



Import main



Class Wire
	Field a:Box
	Field b:Box, bId:Int
	
	Method New( a:Box, b:Box, bId:Int )
		Self.a = a
		Self.b = b
		Self.bId = bId
		
		For Local other:Wire = EachIn PROJ.patch.wires
			If other.b = b And other.bId = bId
				PROJ.patch.wires.Remove( other )
			EndIf
		Next
		
		If a.done
			PROJ.patch.sparks.AddLast( New Spark( Self ) )
		EndIf
	End

	Method Render:Void()
		SetColor 238, 221, 238
		
		If a.done
			SetColor 255, 255, 0
		EndIf
		
		If _dragMode = DRAG_WIRE
			If PROJ.patch.boxOver <> Null And PROJ.patch.boxOver = b And PROJ.patch.inOver = bId
				If Not PROJ.patch.CycleCheck( PROJ.patch.boxOver, from )
					SetColor 255, 0, 0
				EndIf
			EndIf
		ElseIf _dragMode = DRAG_NONE And PROJ.patch.boxOver = b And PROJ.patch.inOver = bId
			SetColor 255, 0, 0
		EndIf
		
		Local x0:Int = a.x + 3
		Local y0:Int = a.y + a.h - 2
		Local x1:Int = b.x + 3 + bId * b.gap
		Local y1:Int = b.y + 1
		DrawLine x0, y0, x1, y1
	End
	
	Function DrawFrom:Void( from:Box, x1:Int, y1:Int )
		SetColor 255, 255, 255
		Local x0:Int = from.x + 3
		Local y0:Int = from.y + from.h - 2
		DrawLine x0, y0, x1, y1
	End
	
	Function DrawFromTo:Void( a:Box, b:Box, bId:Int )
		SetColor 255, 255, 255
		
		If PROJ.patch.CycleCheck( b, a )
			SetColor 255, 0, 0
		EndIf
		
		Local x0:Int = a.x + 3
		Local y0:Int = a.y + a.h - 2
		Local x1:Int = b.x + 3 + bId * b.gap
		Local y1:Int = b.y + 1
		DrawLine x0, y0, x1, y1
	End
End